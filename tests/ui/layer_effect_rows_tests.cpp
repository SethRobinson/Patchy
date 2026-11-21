// The Layers panel's effect rows (GitHub issue 256): a layer's effects listed under its
// row like Smart Filters, with an eye per effect, a master eye, and an Edit pencil or
// double-click that opens the Layer Style dialog on that effect.
#include "core/layer.hpp"
#include "core/layer_metadata.hpp"
#include "core/smart_object.hpp"
#include "core/vector_live_shapes.hpp"
#include "core/vector_raster.hpp"
#include "core/vector_shape.hpp"
#include "ui/layer_thumbnail_size.hpp"
#include "psd/psd_document_io.hpp"
#include "ui/canvas_widget.hpp"
#include "ui/image_document_io.hpp"
#include "ui/qt_paths.hpp"
#include "ui/layer_list_widget.hpp"
#include "ui/main_window.hpp"
#include "ui/theme_manager.hpp"
#include "local_psd_fixtures.hpp"
#include "smart_filter_test_support.hpp"
#include "test_harness.hpp"
#include "ui_test_access.hpp"
#include "ui_test_groups.hpp"
#include "ui_test_support.hpp"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QColor>
#include <QDialog>
#include <QImage>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMenu>
#include <QMouseEvent>
#include <QPointF>
#include <QStringList>
#include <QPoint>
#include <QPointer>
#include <QStackedWidget>
#include <QDir>
#include <QFileInfo>
#include <QStatusBar>
#include <QTest>
#include <QTimer>
#include <QToolButton>
#include <QWidget>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <functional>
#include <iostream>
#include <string>
#include <utility>
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

// The effect rows start closed: opens them through the header's disclosure arrow.
void expand_effects(QListWidget& list, const QString& layer_name = QStringLiteral("Badge")) {
  auto* row = list.itemWidget(require_layer_item(list, layer_name));
  CHECK(row != nullptr);
  auto* disclosure = row->findChild<QToolButton*>(QStringLiteral("layerEffectsDisclosureButton"));
  CHECK(disclosure != nullptr && !disclosure->isChecked());
  const auto global = disclosure->mapToGlobal(disclosure->rect().center());
  click_widget_like_a_user(*list.viewport(), list.viewport()->mapFromGlobal(global));
  for (int pass = 0; pass < 4; ++pass) {
    QApplication::processEvents();
  }
}

int effect_entry_count(QListWidget& list) {
  auto* row = badge_row(list);
  return row == nullptr ? -1 : static_cast<int>(row->findChildren<QWidget*>(QStringLiteral("layerEffectEntryRow")).size());
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

  // Rows: the Layer Styles header, then (once opened) Stroke and Color Overlay in the dialog's
  // order, and the item is tall enough for them (the Smart Filter rule).
  expand_effects(*list);
  auto* row = badge_row(*list);
  CHECK(row != nullptr);
  CHECK(row->findChildren<QWidget*>(QStringLiteral("layerEffectsRow")).size() == 1);
  CHECK(row->findChild<QLabel*>(QStringLiteral("layerEffectsLabel"))->text() == QStringLiteral("Layer Styles"));
  const auto entries = row->findChildren<QWidget*>(QStringLiteral("layerEffectEntryRow"));
  CHECK(entries.size() == 2);
  QStringList names;
  for (auto* entry : entries) {
    names.push_back(entry->findChild<QLabel*>(QStringLiteral("layerEffectEntryLabel"))->text());
  }
  CHECK(names == (QStringList{QStringLiteral("Stroke"), QStringLiteral("Color Overlay")}));
  // The effect rows' tooltips paint a full panel: a row label's own stylesheet once
  // cleared the tip's background through Qt's stylesheet cascade (issue 347).
  for (auto* nested : row->findChildren<QWidget*>()) {
    if (nested->isVisible() && !nested->toolTip().isEmpty() &&
        nested->objectName().startsWith(QStringLiteral("layerEffect"))) {
      const auto share = unpainted_share(render_widget_tooltip(*nested));
      if (share > 0.05) {
        std::cout << "  tooltip of " << nested->objectName().toStdString() << " leaves "
                  << static_cast<int>(share * 100.0) << "% unpainted\n";
      }
      CHECK(share <= 0.05);
    }
  }
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
  expand_effects(*list);
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

// The effect rows start closed and open through the header's disclosure arrow; the state
// survives row rebuilds for the document's life (session memory, not the file), and an effect
// added through the Layer Style dialog opens the list so the new row is in view.
void ui_layer_effect_rows_start_collapsed_and_remember_state() {
  ColorSchemeRestorer restore_scheme;
  ColorSchemeRestorer::apply(patchy::ui::ColorSchemePreference::Dark);
  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(styled_document(true), QStringLiteral("Effect Rows Collapsed"));
  QApplication::processEvents();
  auto* list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  auto* layers_dock = window.findChild<QWidget*>(QStringLiteral("layersDock"));
  CHECK(list != nullptr && layers_dock != nullptr);
  auto& doc = patchy::ui::MainWindowTestAccess::document(window);
  const auto badge_id = *doc.active_layer_id();

  auto* row = badge_row(*list);
  CHECK(row != nullptr);
  CHECK(row->findChildren<QWidget*>(QStringLiteral("layerEffectsRow")).size() == 1);
  CHECK(effect_entry_count(*list) == 0);
  auto* disclosure = row->findChild<QToolButton*>(QStringLiteral("layerEffectsDisclosureButton"));
  CHECK(disclosure != nullptr && !disclosure->isChecked() && disclosure->arrowType() == Qt::RightArrow);
  CHECK(require_layer_item(*list, QStringLiteral("Badge"))->sizeHint().height() == 44 + 26);
  save_widget_artifact("ui_layer_effect_rows_collapsed_dark", *layers_dock);
  ColorSchemeRestorer::apply(patchy::ui::ColorSchemePreference::Light);
  QApplication::processEvents();
  save_widget_artifact("ui_layer_effect_rows_collapsed_light", *layers_dock);
  ColorSchemeRestorer::apply(patchy::ui::ColorSchemePreference::Dark);
  QApplication::processEvents();

  // Open: two rows, the item grows, the arrow turns down; not an edit.
  const auto undo_before = patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);
  expand_effects(*list);
  CHECK(effect_entry_count(*list) == 2);
  row = badge_row(*list);
  disclosure = row->findChild<QToolButton*>(QStringLiteral("layerEffectsDisclosureButton"));
  CHECK(disclosure != nullptr && disclosure->isChecked() && disclosure->arrowType() == Qt::DownArrow);
  CHECK(require_layer_item(*list, QStringLiteral("Badge"))->sizeHint().height() == 44 + 26 * 3);
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == undo_before);

  // A rebuild (hiding another layer through its eye) keeps the list open.
  auto* background_row = list->itemWidget(require_layer_item(*list, QStringLiteral("Background")));
  CHECK(background_row != nullptr);
  click_row_child(*list, *background_row->findChild<QToolButton*>(QStringLiteral("layerVisibilityCheck")));
  CHECK(effect_entry_count(*list) == 2);

  // Close again.
  row = badge_row(*list);
  click_row_child(*list, *row->findChild<QToolButton*>(QStringLiteral("layerEffectsDisclosureButton")));
  CHECK(effect_entry_count(*list) == 0);

  // The footer's Drop Shadow... adds an effect through the dialog; OK opens the list on it.
  auto* styles_button = window.findChild<QToolButton*>(QStringLiteral("layerStylesButton"));
  CHECK(styles_button != nullptr && styles_button->menu() != nullptr);
  QAction* entry = nullptr;
  for (auto* action : styles_button->menu()->actions()) {
    if (action->objectName() == QLatin1String("layerStylesDropShadowAction")) {
      entry = action;
    }
  }
  CHECK(entry != nullptr);
  bool accepted = false;
  QTimer::singleShot(0, [&accepted] {
    if (auto* dialog = find_top_level_dialog(QStringLiteral("patchyLayerStyleDialog"));
        dialog != nullptr && dialog->isVisible()) {
      accepted = true;
      dialog->accept();
    }
  });
  entry->trigger();
  process_events_for(100);
  CHECK(accepted);
  CHECK(std::as_const(doc).find_layer(badge_id)->layer_style().drop_shadows.size() == 1);
  CHECK(effect_entry_count(*list) == 3);
  CHECK(effect_entry_row(*badge_row(*list), QStringLiteral("Drop Shadow")) != nullptr);
}

// Each effect row's eye sits under the Layer Styles eye and its name under "Layer Styles"
// (issue 256 follow-up), in Dark and Light and at the panel's narrowest width, where the
// Edit pencil still fits.
void ui_layer_effect_rows_align_under_the_layer_styles_eye() {
  ColorSchemeRestorer restore_scheme;
  ColorSchemeRestorer::apply(patchy::ui::ColorSchemePreference::Dark);
  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(styled_document(true), QStringLiteral("Effect Rows Align"));
  QApplication::processEvents();
  auto* list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  auto* layers_dock = window.findChild<QWidget*>(QStringLiteral("layersDock"));
  CHECK(list != nullptr && layers_dock != nullptr);
  expand_effects(*list);

  // Alignment is read in each state and checked at the end, so every state's image is saved.
  const auto aligned = [&] {
    auto* row = badge_row(*list);
    auto* viewport = list->viewport();
    if (row == nullptr) {
      return false;
    }
    const auto left_of = [viewport](QWidget* widget) {
      return widget == nullptr ? -1 : widget->mapTo(viewport, QPoint()).x();
    };
    const auto header_eye = left_of(row->findChild<QToolButton*>(QStringLiteral("layerEffectsVisibilityButton")));
    const auto header_label = left_of(row->findChild<QLabel*>(QStringLiteral("layerEffectsLabel")));
    const auto entries = row->findChildren<QWidget*>(QStringLiteral("layerEffectEntryRow"));
    bool ok = entries.size() == 2 && header_eye >= 0 && header_label >= 0;
    for (auto* entry : entries) {
      auto* edit = entry->findChild<QToolButton*>(QStringLiteral("layerEffectEditButton"));
      ok = ok && left_of(entry->findChild<QToolButton*>(QStringLiteral("layerEffectVisibilityButton"))) == header_eye &&
           left_of(entry->findChild<QLabel*>(QStringLiteral("layerEffectEntryLabel"))) == header_label &&
           edit != nullptr && viewport->rect().contains(QRect(edit->mapTo(viewport, QPoint()), edit->size()));
    }
    return ok;
  };
  save_widget_artifact("issue256_effect_rows_dark", *layers_dock);
  const bool dark_aligned = aligned();
  ColorSchemeRestorer::apply(patchy::ui::ColorSchemePreference::Light);
  QApplication::processEvents();
  save_widget_artifact("issue256_effect_rows_light", *layers_dock);
  const bool light_aligned = aligned();
  ColorSchemeRestorer::apply(patchy::ui::ColorSchemePreference::Dark);
  QApplication::processEvents();

  const int narrow_width = patchy::ui::MainWindowTestAccess::right_dock_content_minimum_width(window);
  patchy::ui::MainWindowTestAccess::set_right_dock_stack_width(window, narrow_width);
  patchy::ui::MainWindowTestAccess::refresh_layer_ui(window);
  QApplication::processEvents();
  save_widget_artifact("issue256_effect_rows_compact_dark", *layers_dock);
  CHECK(dark_aligned);
  CHECK(light_aligned);
  CHECK(aligned());
}

// A Smart Object with Smart Filters and a layer style (GitHub issue 349): the "Smart Filters"
// eye and each filter's eye sit in the Layer Styles eye column, the names under "Layer Styles",
// and a click where a filter's eye is drawn toggles that filter.
void ui_layer_smart_filter_eyes_align_with_layer_style_eyes() {
  ColorSchemeRestorer restore_scheme;
  ColorSchemeRestorer::apply(patchy::ui::ColorSchemePreference::Dark);
  patchy::ui::MainWindow window;
  show_window(window);
  patchy::ui::MainWindowTestAccess::open_document_path(
      window, QString::fromStdWString(
                  patchy::test::committed_psd_fixture_path("photoshop-smart-filter-model.psd").wstring()));
  QApplication::processEvents();
  const auto layer_name = QStringLiteral("Applied Median then Gaussian");
  const auto layer_id = select_named_layer(window, layer_name);
  auto& doc = patchy::ui::MainWindowTestAccess::document(window);
  const auto layer = [&] { return std::as_const(doc).find_layer(layer_id); };
  CHECK(layer() != nullptr && layer()->smart_filter_stack() != nullptr);
  CHECK(layer()->smart_filter_stack()->entries.size() == 2U);
  CHECK(layer()->smart_filter_stack()->entries[1].enabled);
  {
    patchy::LayerColorOverlay overlay;
    overlay.enabled = true;
    overlay.color = patchy::RgbColor{20, 200, 40};
    overlay.opacity = 0.5F;
    doc.find_layer(layer_id)->layer_style().color_overlays.push_back(overlay);
    patchy::LayerStroke stroke;
    stroke.enabled = true;
    stroke.size = 2.0F;
    stroke.color = patchy::RgbColor{20, 40, 220};
    doc.find_layer(layer_id)->layer_style().strokes.push_back(stroke);
  }
  patchy::ui::MainWindowTestAccess::refresh_layer_ui(window);
  QApplication::processEvents();
  auto* list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  auto* layers_dock = window.findChild<QWidget*>(QStringLiteral("layersDock"));
  CHECK(list != nullptr && layers_dock != nullptr);
  const auto row = [&] { return list->itemWidget(require_layer_item(*list, layer_name)); };
  // Photoshop saved the layer's rows open (flags bit 5, issue 361), so the new effects show at once.
  CHECK(row() != nullptr && row()->findChild<QToolButton*>(QStringLiteral("layerEffectsDisclosureButton")) != nullptr &&
        row()->findChild<QToolButton*>(QStringLiteral("layerEffectsDisclosureButton"))->isChecked());
  list->scrollToItem(require_layer_item(*list, layer_name));
  QApplication::processEvents();

  // Read in each state and checked at the end, so every state's image is saved.
  const auto aligned = [&] {
    auto* current = row();
    auto* viewport = list->viewport();
    if (current == nullptr) {
      return false;
    }
    const auto left_of = [viewport](QWidget* widget) {
      return widget == nullptr ? -1 : widget->mapTo(viewport, QPoint()).x();
    };
    const auto styles_eye = left_of(current->findChild<QToolButton*>(QStringLiteral("layerEffectsVisibilityButton")));
    const auto styles_label = left_of(current->findChild<QLabel*>(QStringLiteral("layerEffectsLabel")));
    const auto filters_eye =
        left_of(current->findChild<QToolButton*>(QStringLiteral("layerSmartFiltersVisibilityButton")));
    const auto filters_label = left_of(current->findChild<QLabel*>(QStringLiteral("layerSmartFiltersLabel")));
    const auto effect_entries = current->findChildren<QWidget*>(QStringLiteral("layerEffectEntryRow"));
    const auto filter_entries = current->findChildren<QWidget*>(QStringLiteral("layerSmartFilterEntryRow"));
    bool ok = styles_eye >= 0 && styles_label >= 0 && effect_entries.size() == 2 && filter_entries.size() == 2 &&
              filters_eye == styles_eye && filters_label == styles_label;
    if (!ok) {
      std::cerr << "smart filter header eye x " << filters_eye << " label x " << filters_label
                << ", layer styles eye x " << styles_eye << " label x " << styles_label << '\n';
    }
    for (auto* entry : effect_entries) {
      ok = ok && left_of(entry->findChild<QToolButton*>(QStringLiteral("layerEffectVisibilityButton"))) == styles_eye;
    }
    for (auto* entry : filter_entries) {
      const auto eye = left_of(entry->findChild<QToolButton*>(QStringLiteral("layerSmartFilterVisibilityButton")));
      const auto label = left_of(entry->findChild<QLabel*>(QStringLiteral("layerSmartFilterEntryLabel")));
      auto* more = entry->findChild<QToolButton*>(QStringLiteral("layerSmartFilterMoreButton"));
      if (more == nullptr || more->mapTo(viewport, QPoint()).x() + more->width() > viewport->width()) {
        std::cerr << "smart filter entry More button is outside the list\n";
        ok = false;
      }
      if (eye != styles_eye || label != styles_label) {
        std::cerr << "smart filter entry eye x " << eye << " label x " << label << ", layer styles eye x "
                  << styles_eye << " label x " << styles_label << '\n';
        ok = false;
      }
    }
    return ok;
  };
  save_widget_artifact("issue349_smart_filter_eyes_dark", *layers_dock);
  const bool dark_aligned = aligned();
  ColorSchemeRestorer::apply(patchy::ui::ColorSchemePreference::Light);
  QApplication::processEvents();
  save_widget_artifact("issue349_smart_filter_eyes_light", *layers_dock);
  const bool light_aligned = aligned();
  ColorSchemeRestorer::apply(patchy::ui::ColorSchemePreference::Dark);
  QApplication::processEvents();
  patchy::ui::MainWindowTestAccess::set_right_dock_stack_width(
      window, patchy::ui::MainWindowTestAccess::right_dock_content_minimum_width(window));
  patchy::ui::MainWindowTestAccess::refresh_layer_ui(window);
  QApplication::processEvents();
  list->scrollToItem(require_layer_item(*list, layer_name));
  QApplication::processEvents();
  save_widget_artifact("issue349_smart_filter_eyes_compact_dark", *layers_dock);
  const bool compact_aligned = aligned();
  CHECK(dark_aligned);
  CHECK(light_aligned);
  CHECK(compact_aligned);

  // A real click at the center of the first filter row's eye (the Gaussian on top) hides it.
  QToolButton* eye = nullptr;
  for (auto* entry : row()->findChildren<QWidget*>(QStringLiteral("layerSmartFilterEntryRow"))) {
    if (entry->property("smartFilterExecutionIndex").toULongLong() == 1U) {
      eye = entry->findChild<QToolButton*>(QStringLiteral("layerSmartFilterVisibilityButton"));
    }
  }
  CHECK(eye != nullptr && eye->isEnabled() && eye->isChecked());
  const auto eye_center = list->viewport()->mapFromGlobal(eye->mapToGlobal(eye->rect().center()));
  click_widget_like_a_user(*list->viewport(), eye_center);
  CHECK(process_events_until([&] {
    return layer() != nullptr && layer()->smart_filter_stack() != nullptr &&
           !layer()->smart_filter_stack()->entries[1].enabled;
  }));
  CHECK(layer()->smart_filter_stack()->entries[0].enabled);
}

// The Smart Filters header has a disclosure arrow like Layer Styles (issue 361). Photoshop's
// saved open/closed state (layer record flags bit 5) seeds it, a toggle is not an edit, and a
// save writes the panel's state back.
void ui_layer_smart_filter_rows_collapse_and_save_state() {
  ColorSchemeRestorer restore_scheme;
  ColorSchemeRestorer::apply(patchy::ui::ColorSchemePreference::Dark);
  // Photoshop's file with one list closed (a cleared bit 5; the core test pins the bit itself).
  const auto closed_name = QStringLiteral("Gaussian then Hue Saturation");
  const auto open_name = QStringLiteral("Levels then Curves");
  auto source = patchy::psd::DocumentIo::read_file(
      patchy::test::committed_psd_fixture_path("photoshop-smart-filter-adjustments.psd"));
  std::size_t closed_filters = 0;
  std::size_t open_filters = 0;
  for (auto& layer : source.layers()) {
    if (layer.name() == closed_name.toStdString()) {
      layer.metadata()[patchy::kLayerMetadataFxExpanded] = "false";
      closed_filters = layer.smart_filter_stack() != nullptr ? layer.smart_filter_stack()->entries.size() : 0U;
    } else if (layer.name() == open_name.toStdString()) {
      CHECK(patchy::layer_fx_expanded(std::as_const(layer)) == std::optional<bool>{true});
      open_filters = layer.smart_filter_stack() != nullptr ? layer.smart_filter_stack()->entries.size() : 0U;
    }
  }
  CHECK(closed_filters > 0U && open_filters > 0U);
  ensure_artifact_dir();
  const auto input_path =
      QDir(QStringLiteral("test-artifacts")).absoluteFilePath(QStringLiteral("issue361_smart_filters_input.psd"));
  patchy::psd::DocumentIo::write_layered_rgb8_file(source, patchy::ui::to_filesystem_path(input_path));
  patchy::ui::MainWindow window;
  show_window(window);
  // These rows need the Layers list's height, which the expanded Color panel takes in the
  // 780 px test window.
  patchy::ui::MainWindowTestAccess::panel_column(window)->set_panel_expanded(QStringLiteral("colorDock"), false);
  patchy::ui::MainWindowTestAccess::open_document_path(window, input_path);
  QApplication::processEvents();
  CHECK(!patchy::ui::MainWindowTestAccess::active_session_is_modified(window));
  auto* list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  auto* layers_dock = window.findChild<QWidget*>(QStringLiteral("layersDock"));
  CHECK(list != nullptr && layers_dock != nullptr);
  const auto row = [&](const QString& name) { return list->itemWidget(require_layer_item(*list, name)); };
  const auto disclosure = [&](const QString& name) {
    auto* current = row(name);
    return current == nullptr
               ? nullptr
               : current->findChild<QToolButton*>(QStringLiteral("layerSmartFiltersDisclosureButton"));
  };
  const auto entry_count = [&](const QString& name) {
    auto* current = row(name);
    return current == nullptr
               ? -1
               : static_cast<int>(current->findChildren<QWidget*>(QStringLiteral("layerSmartFilterEntryRow")).size());
  };
  const auto item_height = [&](const QString& name) {
    return require_layer_item(*list, name)->sizeHint().height();
  };

  const auto open_rows = static_cast<int>(open_filters);
  const auto closed_rows = static_cast<int>(closed_filters);
  CHECK(disclosure(closed_name) != nullptr && !disclosure(closed_name)->isChecked() &&
        disclosure(closed_name)->arrowType() == Qt::RightArrow);
  CHECK(entry_count(closed_name) == 0);
  CHECK(item_height(closed_name) == 44 + 26);
  CHECK(disclosure(open_name) != nullptr && disclosure(open_name)->isChecked() &&
        disclosure(open_name)->arrowType() == Qt::DownArrow);
  CHECK(entry_count(open_name) == open_rows);
  CHECK(item_height(open_name) == 44 + 26 * (1 + open_rows));
  // The header still reads "Smart Filters" with its eye after the arrow.
  auto* closed_row = row(closed_name);
  CHECK(closed_row != nullptr &&
        closed_row->findChild<QToolButton*>(QStringLiteral("layerSmartFiltersVisibilityButton")) != nullptr);
  list->scrollToItem(require_layer_item(*list, closed_name));
  QApplication::processEvents();
  save_widget_artifact("issue361_smart_filters_collapsed_and_open_dark", *layers_dock);
  ColorSchemeRestorer::apply(patchy::ui::ColorSchemePreference::Light);
  QApplication::processEvents();
  save_widget_artifact("issue361_smart_filters_collapsed_and_open_light", *layers_dock);
  ColorSchemeRestorer::apply(patchy::ui::ColorSchemePreference::Dark);
  QApplication::processEvents();

  // Real clicks swap the two states; neither is an edit.
  const auto undo_before = patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);
  click_row_child(*list, *disclosure(closed_name));
  click_row_child(*list, *disclosure(open_name));
  CHECK(entry_count(closed_name) == closed_rows);
  CHECK(item_height(closed_name) == 44 + 26 * (1 + closed_rows));
  CHECK(disclosure(closed_name) != nullptr && disclosure(closed_name)->isChecked());
  CHECK(entry_count(open_name) == 0);
  CHECK(item_height(open_name) == 44 + 26);
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == undo_before);
  CHECK(!patchy::ui::MainWindowTestAccess::active_session_is_modified(window));

  // Layer styles on the Smart Object without filters, opened through their own arrow.
  {
    auto& doc = patchy::ui::MainWindowTestAccess::document(window);
    for (auto& layer : doc.layers()) {
      if (layer.name() == "Source") {
        patchy::LayerStroke stroke;
        stroke.enabled = true;
        stroke.size = 2.0F;
        stroke.color = patchy::RgbColor{20, 40, 220};
        layer.layer_style().strokes.push_back(stroke);
      }
    }
    patchy::ui::MainWindowTestAccess::refresh_layer_ui(window);
    QApplication::processEvents();
    list->scrollToItem(require_layer_item(*list, QStringLiteral("Source")));
    QApplication::processEvents();
    expand_effects(*list, QStringLiteral("Source"));
    auto* source_disclosure =
        row(QStringLiteral("Source"))->findChild<QToolButton*>(QStringLiteral("layerEffectsDisclosureButton"));
    CHECK(source_disclosure != nullptr && source_disclosure->isChecked());
  }

  // A save writes the panel's state: the file reopens the way it was left.
  const auto saved_path =
      QDir(QStringLiteral("test-artifacts")).absoluteFilePath(QStringLiteral("issue361_smart_filters_resaved.psd"));
  CHECK(patchy::ui::MainWindowTestAccess::save_document_to_path(window, saved_path));
  const auto saved = patchy::psd::DocumentIo::read_file(patchy::ui::to_filesystem_path(saved_path));
  const auto expanded_named = [&saved](const std::string& name) {
    for (const auto& layer : saved.layers()) {
      if (layer.name() == name) {
        return patchy::layer_fx_expanded(layer);
      }
    }
    return std::optional<bool>{};
  };
  CHECK(expanded_named(closed_name.toStdString()) == std::optional<bool>{true});
  CHECK(expanded_named(open_name.toStdString()) == std::optional<bool>{false});
  CHECK(expanded_named("Source") == std::optional<bool>{true});
}

// With the master eye off, each effect's eye draws dimmed (not slashed) and still toggles
// that effect's own flag; the composite stays unstyled until the master is back on.
void ui_layer_effect_rows_grey_eyes_under_master_off() {
  ColorSchemeRestorer restore_scheme;
  ColorSchemeRestorer::apply(patchy::ui::ColorSchemePreference::Dark);
  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(styled_document(true), QStringLiteral("Effect Rows Master"));
  QApplication::processEvents();
  auto* list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  auto* layers_dock = window.findChild<QWidget*>(QStringLiteral("layersDock"));
  CHECK(list != nullptr && layers_dock != nullptr);
  auto& doc = patchy::ui::MainWindowTestAccess::document(window);
  const auto badge_id = *doc.active_layer_id();
  const auto style = [&]() -> const patchy::LayerStyle& { return std::as_const(doc).find_layer(badge_id)->layer_style(); };
  expand_effects(*list);

  auto* row = badge_row(*list);
  auto* stroke_entry = effect_entry_row(*row, QStringLiteral("Stroke"));
  CHECK(stroke_entry != nullptr);
  auto* stroke_eye = stroke_entry->findChild<QToolButton*>(QStringLiteral("layerEffectVisibilityButton"));
  CHECK(stroke_eye != nullptr && !stroke_eye->property("layerEffectEyeDimmed").toBool());
  const auto normal_icon = stroke_eye->icon().pixmap(QSize(15, 15)).toImage();

  click_row_child(*list, *row->findChild<QToolButton*>(QStringLiteral("layerEffectsVisibilityButton")));
  CHECK(!style().effects_visible);
  CHECK(style().strokes.front().enabled);
  row = badge_row(*list);
  stroke_entry = effect_entry_row(*row, QStringLiteral("Stroke"));
  CHECK(stroke_entry != nullptr);
  stroke_eye = stroke_entry->findChild<QToolButton*>(QStringLiteral("layerEffectVisibilityButton"));
  CHECK(stroke_eye != nullptr && stroke_eye->isChecked());  // its own state: still on
  CHECK(stroke_eye->property("layerEffectEyeDimmed").toBool());
  CHECK(stroke_eye->isEnabled());
  const auto dimmed_icon = stroke_eye->icon().pixmap(QSize(15, 15)).toImage();
  CHECK(dimmed_icon != normal_icon);
  save_widget_artifact("ui_layer_effect_rows_master_off_dark", *layers_dock);
  ColorSchemeRestorer::apply(patchy::ui::ColorSchemePreference::Light);
  QApplication::processEvents();
  save_widget_artifact("ui_layer_effect_rows_master_off_light", *layers_dock);
  ColorSchemeRestorer::apply(patchy::ui::ColorSchemePreference::Dark);
  QApplication::processEvents();

  // Toggling under the master still flips the flag; the composite stays plain red.
  row = badge_row(*list);
  stroke_entry = effect_entry_row(*row, QStringLiteral("Stroke"));
  click_row_child(*list, *stroke_entry->findChild<QToolButton*>(QStringLiteral("layerEffectVisibilityButton")));
  CHECK(!style().strokes.front().enabled);
  CHECK(!style().effects_visible);
  const auto center = composite_pixel(window, 32, 24);
  CHECK(center.red() > 150 && center.green() < 100);
  const auto ring = composite_pixel(window, 20, 24);
  CHECK(ring.red() > 240 && ring.green() > 240 && ring.blue() > 240);
  row = badge_row(*list);
  stroke_entry = effect_entry_row(*row, QStringLiteral("Stroke"));
  stroke_eye = stroke_entry->findChild<QToolButton*>(QStringLiteral("layerEffectVisibilityButton"));
  CHECK(stroke_eye != nullptr && !stroke_eye->isChecked() && stroke_eye->property("layerEffectEyeDimmed").toBool());

  // Master back on: the overlay returns, the stroke stays off by its own flag.
  click_row_child(*list, *row->findChild<QToolButton*>(QStringLiteral("layerEffectsVisibilityButton")));
  CHECK(style().effects_visible && !style().strokes.front().enabled);
  CHECK(composite_pixel(window, 32, 24).green() > 150);
  const auto ring_after = composite_pixel(window, 20, 24);
  CHECK(ring_after.red() > 240 && ring_after.green() > 240);  // white: no blue ring
  row = badge_row(*list);
  stroke_entry = effect_entry_row(*row, QStringLiteral("Stroke"));
  CHECK(!stroke_entry->findChild<QToolButton*>(QStringLiteral("layerEffectVisibilityButton"))
             ->property("layerEffectEyeDimmed")
             .toBool());
}

// Polls for the effect row's context menu, hands it to `act`, then closes it.
void handle_effect_context_menu(std::function<void(QMenu&)> act, std::shared_ptr<int> seen, int attempts = 100) {
  QTimer::singleShot(10, [act = std::move(act), seen, attempts]() mutable {
    for (auto* widget : QApplication::topLevelWidgets()) {
      auto* menu = qobject_cast<QMenu*>(widget);
      if (menu != nullptr && menu->objectName() == QStringLiteral("layerEffectContextMenu") && menu->isVisible()) {
        ++*seen;
        act(*menu);
        menu->close();
        return;
      }
    }
    if (attempts > 0) {
      handle_effect_context_menu(std::move(act), seen, attempts - 1);
    }
  });
}

QAction* menu_action(QMenu& menu, const char* name) {
  for (auto* action : menu.actions()) {
    if (action->objectName() == QLatin1String(name)) {
      return action;
    }
  }
  return nullptr;
}

// Right-clicking an effect row opens that effect's menu (not the layer menu): Edit, Hide/Show,
// Delete. Delete removes the instance as one undo step, the row goes, and the saved PSD drops
// it; a locked layer keeps Delete and Hide disabled.
void ui_layer_effect_row_context_menu_edits_hides_and_deletes() {
  ColorSchemeRestorer restore_scheme;
  ColorSchemeRestorer::apply(patchy::ui::ColorSchemePreference::Dark);
  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(styled_document(true), QStringLiteral("Effect Row Menu"));
  QApplication::processEvents();
  auto* list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(list != nullptr);
  auto& doc = patchy::ui::MainWindowTestAccess::document(window);
  const auto badge_id = *doc.active_layer_id();
  const auto style = [&]() -> const patchy::LayerStyle& { return std::as_const(doc).find_layer(badge_id)->layer_style(); };
  expand_effects(*list);

  const auto stroke_row_position = [&] {
    auto* entry = effect_entry_row(*badge_row(*list), QStringLiteral("Stroke"));
    CHECK(entry != nullptr);
    auto* label = entry->findChild<QLabel*>(QStringLiteral("layerEffectEntryLabel"));
    return list->viewport()->mapFromGlobal(label->mapToGlobal(label->rect().center()));
  };

  // The menu's shape, captured; nothing chosen.
  auto seen = std::make_shared<int>(0);
  handle_effect_context_menu(
      [](QMenu& menu) {
        CHECK(menu_action(menu, "layerEffectEditAction") != nullptr);
        CHECK(menu_action(menu, "layerEffectVisibilityAction") != nullptr);
        CHECK(menu_action(menu, "layerEffectVisibilityAction")->text().contains(QStringLiteral("Hide")));
        CHECK(menu_action(menu, "layerEffectDeleteAction") != nullptr);
        CHECK(menu_action(menu, "layerEffectDeleteAction")->isEnabled());
        save_widget_artifact("ui_layer_effect_row_context_menu_dark", menu);
      },
      seen);
  patchy::ui::MainWindowTestAccess::show_layer_context_menu(window, stroke_row_position());
  process_events_for(100);
  CHECK(*seen == 1);

  // Hide through the menu flips the flag.
  handle_effect_context_menu([](QMenu& menu) { menu_action(menu, "layerEffectVisibilityAction")->trigger(); }, seen);
  patchy::ui::MainWindowTestAccess::show_layer_context_menu(window, stroke_row_position());
  process_events_for(100);
  CHECK(*seen == 2);
  CHECK(!style().strokes.front().enabled);

  // Delete removes the instance: one undo step, the row is gone, the PSD drops it.
  const auto undo_before = patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);
  handle_effect_context_menu([](QMenu& menu) { menu_action(menu, "layerEffectDeleteAction")->trigger(); }, seen);
  patchy::ui::MainWindowTestAccess::show_layer_context_menu(window, stroke_row_position());
  process_events_for(100);
  CHECK(*seen == 3);
  CHECK(style().strokes.empty());
  CHECK(style().color_overlays.size() == 1);
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == undo_before + 1);
  CHECK(effect_entry_count(*list) == 1);
  CHECK(effect_entry_row(*badge_row(*list), QStringLiteral("Stroke")) == nullptr);
  ensure_artifact_dir();
  const auto psd_path =
      QFileInfo(QDir(QStringLiteral("test-artifacts")).filePath(QStringLiteral("effect-row-delete.psd"))).absoluteFilePath();
  patchy::psd::DocumentIo::write_layered_rgb8_file(std::as_const(doc), patchy::ui::to_filesystem_path(psd_path));
  const auto reread = patchy::psd::DocumentIo::read_file(patchy::ui::to_filesystem_path(psd_path));
  CHECK(reread.layers().size() == 2);
  CHECK(reread.layers().back().layer_style().strokes.empty());
  CHECK(reread.layers().back().layer_style().color_overlays.size() == 1);
  patchy::ui::MainWindowTestAccess::undo(window);
  QApplication::processEvents();
  CHECK(style().strokes.size() == 1 && !style().strokes.front().enabled);
  CHECK(effect_entry_count(*list) == 2);

  // A locked layer: the menu still opens, Delete and Hide/Show are disabled.
  patchy::set_layer_lock_flags(*doc.find_layer(badge_id), patchy::kLayerLockAll);
  patchy::ui::MainWindowTestAccess::refresh_layer_ui(window);
  QApplication::processEvents();
  if (effect_entry_count(*list) == 0) {
    expand_effects(*list);
  }
  handle_effect_context_menu(
      [](QMenu& menu) {
        CHECK(!menu_action(menu, "layerEffectDeleteAction")->isEnabled());
        CHECK(!menu_action(menu, "layerEffectVisibilityAction")->isEnabled());
        CHECK(menu_action(menu, "layerEffectEditAction")->isEnabled());
      },
      seen);
  patchy::ui::MainWindowTestAccess::show_layer_context_menu(window, stroke_row_position());
  process_events_for(100);
  CHECK(*seen == 4);
}

// A right-click on the fx badge (#473) opens Copy, Paste and Delete Layer Style and the Layer Styles
// master switch for that row; it does not open Layer Style (the left click) or the row's menu.
void ui_layer_fx_badge_right_click_opens_style_menu() {
  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(styled_document(true), QStringLiteral("Fx Badge Menu"));
  QApplication::processEvents();
  auto* list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(list != nullptr);
  auto& doc = patchy::ui::MainWindowTestAccess::document(window);
  const auto badge_id = *doc.active_layer_id();
  const auto badge_layer = [&] { return std::as_const(doc).find_layer(badge_id); };
  // The Background row is the selected, active one first.
  auto* background_item = require_layer_item(*list, QStringLiteral("Background"));
  list->clearSelection();
  list->setCurrentItem(background_item);
  background_item->setSelected(true);
  QApplication::processEvents();
  CHECK(doc.active_layer_id() != badge_id);

  const auto fx_menu = [&](const char* pick, const std::string& artifact = {}) {
    return right_click_layer_row_widget(*list, QStringLiteral("Badge"), QStringLiteral("layerFxBadgeButton"),
                                        QString::fromLatin1(pick), artifact);
  };
  // Any dialog opening during the right click (Layer Style is the left click's) is closed and noted.
  bool dialog_opened = false;
  QTimer dialog_guard;
  QObject::connect(&dialog_guard, &QTimer::timeout, [&dialog_opened] {
    if (auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget()); dialog != nullptr) {
      dialog_opened = true;
      dialog->reject();
    }
  });
  dialog_guard.start(20);
  auto seen = fx_menu("", "issue473_fx_badge_menu");
  process_events_for(100);
  dialog_guard.stop();
  CHECK(!dialog_opened);
  CHECK(seen.name == QStringLiteral("layerFxBadgeMenu"));
  CHECK(seen.actions == (QStringList{QStringLiteral("layerCopyStyleAction"), QStringLiteral("layerPasteStyleAction"),
                                     QStringLiteral("layerDeleteStyleAction"), QStringLiteral("-"),
                                     QStringLiteral("layerFxBadgeShowStylesAction")}));
  CHECK(seen.texts.value(0) == QStringLiteral("Copy Layer Style"));
  CHECK(seen.texts.value(4) == QStringLiteral("Show Layer Styles"));
  CHECK(seen.enabled.value(0) && !seen.enabled.value(1) && seen.enabled.value(2));  // nothing copied yet
  CHECK(seen.enabled.value(4) && seen.checked.value(4));
  CHECK(doc.active_layer_id() == badge_id);
  CHECK(require_layer_item(*list, QStringLiteral("Badge"))->isSelected());

  // The switch hides and shows the styles: one undo step each.
  const auto undo_before = patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);
  fx_menu("layerFxBadgeShowStylesAction");
  CHECK(!badge_layer()->layer_style().effects_visible);
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == undo_before + 1);
  seen = fx_menu("layerFxBadgeShowStylesAction");
  CHECK(!seen.checked.value(4));
  CHECK(badge_layer()->layer_style().effects_visible);
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == undo_before + 2);

  // Copy fills the clipboard (Paste turns on); Delete clears the clicked row's styles.
  fx_menu("layerCopyStyleAction");
  seen = fx_menu("layerDeleteStyleAction");
  CHECK(seen.enabled.value(1));
  CHECK(badge_layer()->layer_style().empty());
  CHECK(badge_row(*list)->findChild<QToolButton*>(QStringLiteral("layerFxBadgeButton")) == nullptr);
}

// GitHub issue 494: a layer or folder whose advanced blending is not the default shows the
// blending badge, its tooltip names what differs, and a click opens Blending Options. The
// document goes through a PSD save and open first, so the badge reads what the file holds.
void ui_layer_advanced_blending_badge_names_settings_and_opens_blending_options() {
  auto source = styled_document(true);
  auto plain_pixels = solid_pixels(64, 48, patchy::PixelFormat::rgba8(), QColor(40, 90, 200, 255));
  auto& blended = source.add_pixel_layer("Blended", plain_pixels);
  patchy::set_layer_knockout(blended, 2U);
  blended.layer_style().blend_clipped_elements = false;
  auto& shapes_off = source.add_pixel_layer("Shapes Off", plain_pixels);
  shapes_off.unknown_psd_blocks().push_back({"tsly", {0U, 0U, 0U, 0U}});
  shapes_off.layer_style().layer_mask_hides_effects = true;
  source.add_pixel_layer("Plain", plain_pixels);
  patchy::Layer folder(source.allocate_layer_id(), "Folder", patchy::LayerKind::Group);
  folder.set_blend_mode(patchy::BlendMode::PassThrough);
  patchy::set_layer_knockout(folder, 1U);
  folder.children().emplace_back(source.allocate_layer_id(), "Inside", plain_pixels);
  source.add_layer(std::move(folder));
  ensure_artifact_dir();
  const auto input_path =
      QDir(QStringLiteral("test-artifacts")).absoluteFilePath(QStringLiteral("issue494_advanced_blending.psd"));
  patchy::psd::DocumentIo::write_layered_rgb8_file(source, patchy::ui::to_filesystem_path(input_path));

  patchy::ui::MainWindow window;
  show_window(window);
  // Every row in view: the expanded Color panel takes the Layers list's height in the test window.
  patchy::ui::MainWindowTestAccess::panel_column(window)->set_panel_expanded(QStringLiteral("colorDock"), false);
  patchy::ui::MainWindowTestAccess::open_document_path(window, input_path);
  QApplication::processEvents();
  auto* list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(list != nullptr);
  const auto blending_badge = [&](const QString& name) -> QToolButton* {
    auto* row = list->itemWidget(require_layer_item(*list, name));
    return row == nullptr ? nullptr : row->findChild<QToolButton*>(QStringLiteral("layerBlendingBadgeButton"));
  };
  if (auto* layers_dock = window.findChild<QWidget*>(QStringLiteral("layersDock")); layers_dock != nullptr) {
    save_widget_artifact("issue494_advanced_blending_badges", *layers_dock);
  }
  CHECK(blending_badge(QStringLiteral("Plain")) == nullptr);
  CHECK(blending_badge(QStringLiteral("Badge")) == nullptr);  // layer styles alone are not advanced blending
  CHECK(blending_badge(QStringLiteral("Inside")) == nullptr);
  auto* badge = blending_badge(QStringLiteral("Blended"));
  CHECK(badge != nullptr);
  CHECK(badge != nullptr &&
        badge->toolTip() == QStringLiteral("Advanced blending: Knockout Deep, Blend Clipped Layers as Group off. "
                                           "Click to open Blending Options."));
  auto* shapes_badge = blending_badge(QStringLiteral("Shapes Off"));
  CHECK(shapes_badge != nullptr &&
        shapes_badge->toolTip() == QStringLiteral("Advanced blending: Layer Mask Hides Effects on, Transparency "
                                                  "Shapes Layer off. Click to open Blending Options."));
  auto* folder_badge = blending_badge(QStringLiteral("Folder"));
  CHECK(folder_badge != nullptr &&
        folder_badge->toolTip() == QStringLiteral("Advanced blending: Knockout Shallow. Click to open Blending Options."));
  if (badge == nullptr) {
    return;
  }

  // The click opens Layer Style on Blending Options; setting both back to their defaults
  // there drops the badge, and undo brings it back.
  QString current_label;
  bool found = false;
  const std::function<void(int)> watch_dialog = [&](int attempts) {
    QTimer::singleShot(20, [&, attempts] {
      auto* dialog = find_top_level_dialog(QStringLiteral("patchyLayerStyleDialog"));
      if (dialog == nullptr || !dialog->isVisible()) {
        if (attempts > 0) {
          watch_dialog(attempts - 1);
        }
        return;
      }
      found = true;
      auto* categories = dialog->findChild<QListWidget*>(QStringLiteral("layerStyleCategoryList"));
      if (categories != nullptr && categories->currentItem() != nullptr) {
        current_label = categories->currentItem()->text();
      }
      auto* knockout = dialog->findChild<QComboBox*>(QStringLiteral("layerStyleKnockoutCombo"));
      auto* blend_clipped = dialog->findChild<QCheckBox*>(QStringLiteral("layerStyleBlendClippedCheck"));
      if (knockout == nullptr || blend_clipped == nullptr) {
        dialog->reject();
        return;
      }
      knockout->setCurrentIndex(knockout->findData(0));
      blend_clipped->setChecked(true);
      dialog->accept();
    });
  };
  list->scrollToItem(require_layer_item(*list, QStringLiteral("Blended")));
  QApplication::processEvents();
  badge = blending_badge(QStringLiteral("Blended"));
  CHECK(badge != nullptr && badge->isVisible());
  watch_dialog(100);
  click_row_child(*list, *badge);
  process_events_for(300);
  CHECK(found);
  CHECK(current_label == QStringLiteral("Blending Options"));
  CHECK(blending_badge(QStringLiteral("Blended")) == nullptr);
  patchy::ui::MainWindowTestAccess::undo(window);
  QApplication::processEvents();
  badge = blending_badge(QStringLiteral("Blended"));
  CHECK(badge != nullptr && badge->toolTip().contains(QStringLiteral("Knockout Deep")));
}

// The follow-up to GitHub issue 494: rows that carry every row indicator at once (blending,
// fx, smart object, vector shape, both mask thumbnails and links, the clip badge, all four
// locks, a color label, long names), captured at the narrowest panel and at 460 px, at every
// thumbnail size, in both schemes. Each row's name keeps a few characters and ends before its
// first badge, no badge overlaps another or leaves the row, and where the panel has room the
// row fits it (the lock badges fold into one padlock first).
void ui_layer_rows_with_every_badge_fit_narrow_and_wide() {
  ColorSchemeRestorer restore_scheme;
  SettingsValueRestorer restore_size{QString::fromLatin1(patchy::ui::kLayerThumbnailSizeSettingsKey)};
  patchy::ui::MainWindow window;
  show_window(window);
  patchy::ui::MainWindowTestAccess::panel_column(window)->set_panel_expanded(QStringLiteral("colorDock"), false);

  constexpr int kWidth = 320;
  constexpr int kHeight = 200;
  patchy::Document document(kWidth, kHeight, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background", solid_pixels(kWidth, kHeight, patchy::PixelFormat::rgba8(), QColor(255, 255, 255)));
  auto square = [&](QColor color) {
    auto pixels = solid_pixels(kWidth, kHeight, patchy::PixelFormat::rgba8(), QColor(0, 0, 0, 0));
    fill_pixel_rect(pixels, QRect(60, 40, 200, 120), color);
    return pixels;
  };
  document.add_pixel_layer("Clip base", square(QColor(40, 120, 220)));
  // A shape layer clipped to the base.
  {
    patchy::Layer shape(document.allocate_layer_id(), "Shape layer with styles, masks, locks and blending",
                        patchy::PixelBuffer());
    patchy::LiveShapeParams params;
    params.kind = patchy::LiveShapeKind::Rectangle;
    params.left = 80; params.top = 60; params.right = 240; params.bottom = 140; params.index = 0;
    patchy::populate_live_shape_box_corners(params);
    patchy::VectorShapeContent content;
    content.path.subpaths = patchy::generate_live_shape_subpaths(params);
    content.origination.push_back(params);
    content.fill.kind = patchy::VectorFillKind::Solid;
    content.fill.color = patchy::RgbColor{240, 180, 40};
    shape.set_vector_shape(std::move(content));
    shape.metadata()[patchy::kLayerMetadataVectorShape] = "1";
    patchy::update_vector_shape_raster(shape, patchy::Rect::from_size(kWidth, kHeight), nullptr);
    document.add_layer(std::move(shape));
  }
  const auto smart_id = document.add_pixel_layer(
      "Every badge at once on one smart object layer with a long name", square(QColor(220, 60, 60))).id();
  patchy::Layer folder(document.allocate_layer_id(), "Folder with styles, masks, locks and knockout",
                       patchy::LayerKind::Group);
  folder.set_blend_mode(patchy::BlendMode::PassThrough);
  folder.children().emplace_back(document.allocate_layer_id(), "Inside the folder", square(QColor(60, 200, 90)));
  document.add_layer(std::move(folder));
  document.set_active_layer(smart_id);
  window.add_document_session(std::move(document), QStringLiteral("All Badges"));
  QApplication::processEvents();
  require_action(window, "layerConvertSmartObjectAction")->trigger();
  QApplication::processEvents();

  auto& doc = patchy::ui::MainWindowTestAccess::document(window);
  patchy::LayerStroke stroke;
  stroke.enabled = true;
  stroke.size = 3.0F;
  stroke.color = patchy::RgbColor{20, 20, 20};
  stroke.opacity = 1.0F;
  const auto dress = [&](patchy::Layer& layer, patchy::LayerColorLabel color, bool clipped) {
    layer.layer_style().strokes.push_back(stroke);
    patchy::set_layer_knockout(layer, layer.kind() == patchy::LayerKind::Group ? 1U : 2U);
    layer.layer_style().blend_clipped_elements = false;
    patchy::LayerMask mask;
    mask.bounds = patchy::Rect{0, 0, kWidth, kHeight};
    mask.pixels = patchy::PixelBuffer(kWidth, kHeight, patchy::PixelFormat::gray8());
    const auto mask_bytes = mask.pixels.data();
    std::fill(mask_bytes.begin(), mask_bytes.end(), std::uint8_t{255});
    layer.set_mask(std::move(mask));
    patchy::LiveShapeParams params;
    params.kind = patchy::LiveShapeKind::Rectangle;
    params.left = 20; params.top = 20; params.right = 300; params.bottom = 180; params.index = 0;
    patchy::populate_live_shape_box_corners(params);
    patchy::LayerVectorMask vector_mask;
    vector_mask.path.subpaths = patchy::generate_live_shape_subpaths(params);
    layer.set_vector_mask(std::move(vector_mask));
    patchy::update_vector_mask_raster(layer, patchy::Rect{0, 0, kWidth, kHeight});
    layer.set_color_label(color);
    layer.set_clipped(clipped);
    layer.set_lock_flags(patchy::kLayerLockAll | patchy::kLayerLockArtboardAutonest);
  };
  bool smart_found = false;
  std::vector<QString> dressed_names;
  for (auto& layer : doc.layers()) {
    const auto name = QString::fromStdString(layer.name());
    if (patchy::layer_is_smart_object(std::as_const(layer))) {
      smart_found = true;
      dress(layer, patchy::kLayerColorRed, true);
    } else if (patchy::layer_is_vector_shape(std::as_const(layer))) {
      dress(layer, patchy::kLayerColorYellow, true);
    } else if (layer.kind() == patchy::LayerKind::Group) {
      dress(layer, patchy::kLayerColorViolet, false);
    } else {
      continue;
    }
    dressed_names.push_back(name);
  }
  CHECK(smart_found);
  CHECK(dressed_names.size() == 3U);
  patchy::ui::MainWindowTestAccess::refresh_layer_ui(window);
  QApplication::processEvents();

  ensure_artifact_dir();
  const auto psd_path =
      QDir(QStringLiteral("test-artifacts")).absoluteFilePath(QStringLiteral("issue494_all_badges.psd"));
  CHECK(patchy::ui::MainWindowTestAccess::save_document_to_path(window, psd_path));

  auto* list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  auto* layers_dock = window.findChild<QWidget*>(QStringLiteral("layersDock"));
  CHECK(list != nullptr && layers_dock != nullptr);
  const auto badge_names = QStringList{QStringLiteral("layerBlendingBadgeButton"), QStringLiteral("layerFxBadgeButton"),
                                       QStringLiteral("layerSmartObjectBadgeButton"),
                                       QStringLiteral("layerVectorBadgeButton"), QStringLiteral("layerLockBadge"),
                                       QStringLiteral("layerLockSummaryBadge")};
  int failures = 0;
  const auto check_rows = [&](const QString& label, bool must_fit) {
    for (const auto& name : dressed_names) {
      auto* row = list->itemWidget(require_layer_item(*list, name));
      auto* main_row = row != nullptr ? row->findChild<QWidget*>(QStringLiteral("layerMainRow")) : nullptr;
      auto* name_label = main_row != nullptr ? main_row->findChild<QLabel*>(QStringLiteral("layerRowName")) : nullptr;
      if (name_label == nullptr) {
        ++failures;
        std::cout << "[BADGES] " << label.toStdString() << " missing row parts for " << name.toStdString() << '\n';
        continue;
      }
      const QRect name_rect(name_label->mapTo(main_row, QPoint()), name_label->size());
      std::vector<std::pair<QString, QRect>> badges;
      for (auto* child : main_row->findChildren<QWidget*>()) {
        if (child->isVisible() && badge_names.contains(child->objectName())) {
          badges.emplace_back(child->objectName(), QRect(child->mapTo(main_row, QPoint()), child->size()));
        }
      }
      std::sort(badges.begin(), badges.end(),
                [](const auto& a, const auto& b) { return a.second.left() < b.second.left(); });
      std::string line = "[BADGES] " + label.toStdString() + " row=\"" + name.left(24).toStdString() +
                         "\" main=" + std::to_string(main_row->width()) + " name=" +
                         std::to_string(name_rect.left()) + ".." + std::to_string(name_rect.right());
      for (const auto& [object_name, rect] : badges) {
        line += " " + object_name.mid(5, 6).toStdString() + "=" + std::to_string(rect.left()) + ".." +
                std::to_string(rect.right()) + "@" + std::to_string(rect.top()) + "h" + std::to_string(rect.height());
      }
      const bool fits = main_row->width() <= list->viewport()->width();
      line += fits ? " fits" : " overflows";
      bool ok = badges.size() >= 3U && name_rect.width() >= name_label->fontMetrics().horizontalAdvance(QStringLiteral("Www")) &&
                name_rect.right() < badges.front().second.left() && (fits || !must_fit);
      for (std::size_t index = 0; index < badges.size(); ++index) {
        // Badges keep their size however crowded the row: 20 px buttons, 18 px lock marks.
        const int expected = badges[index].first.endsWith(QStringLiteral("Button")) ? 20 : 18;
        ok = ok && badges[index].second.width() == expected && badges[index].second.height() == expected;
        ok = ok && main_row->rect().contains(badges[index].second);
        if (index > 0) ok = ok && !badges[index].second.intersects(badges[index - 1].second);
      }
      if (!ok) {
        ++failures;
        line += "  <- PROBLEM";
      }
      std::cout << line << '\n';
    }
  };

  const int minimum_width = patchy::ui::MainWindowTestAccess::right_dock_content_minimum_width(window);
  const std::array<std::pair<patchy::ui::LayerThumbnailSize, const char*>, 4> sizes{{
      {patchy::ui::LayerThumbnailSize::None, "none"},
      {patchy::ui::LayerThumbnailSize::Small, "small"},
      {patchy::ui::LayerThumbnailSize::Medium, "medium"},
      {patchy::ui::LayerThumbnailSize::Large, "large"},
  }};
  for (const auto scheme : {patchy::ui::ColorSchemePreference::Dark, patchy::ui::ColorSchemePreference::Light}) {
    ColorSchemeRestorer::apply(scheme);
    const std::string scheme_name = scheme == patchy::ui::ColorSchemePreference::Dark ? "dark" : "light";
    for (const auto& [size, size_name] : sizes) {
      patchy::ui::MainWindowTestAccess::set_layer_thumbnail_size(window, size);
      for (const auto width : {minimum_width, 460}) {
        patchy::ui::MainWindowTestAccess::set_right_dock_stack_width(window, width);
        patchy::ui::MainWindowTestAccess::refresh_layer_ui(window);
        for (int pass = 0; pass < 4; ++pass) QApplication::processEvents();
        const auto label = scheme_name + "_" + size_name + "_" + std::to_string(width);
        save_widget_artifact("issue494_all_badges_" + label, *layers_dock);
        // Every indicator at once cannot fit beside Medium or Large thumbnails in the narrowest
        // panel, nor beside Large ones at 460 px: the list scrolls sideways there. macOS's wider
        // system font adds Small thumbnails in the narrowest panel to that (accepted for 1.21).
#ifdef Q_OS_MACOS
        const bool small_fits = width >= 460;
#else
        const bool small_fits = true;
#endif
        const bool must_fit = size == patchy::ui::LayerThumbnailSize::None ||
                              (size == patchy::ui::LayerThumbnailSize::Small && small_fits) ||
                              (width >= 460 && size == patchy::ui::LayerThumbnailSize::Medium);
        check_rows(QString::fromStdString(label), must_fit);
      }
    }
  }
  std::cout << "[BADGES] minimum width " << minimum_width << ", dock width now " << layers_dock->width() << '\n';
  CHECK(failures == 0);
}

// local-063: a press on one effect's eye and a drag along the eye column sets every effect
// eye crossed to the state the press produced.
//
// A white 240x160 document with a red 80x80 "Paint Layer" square at (80,40) whose style
// lists, in the rows' order, Bevel & Emboss, a blue 6 px outside Stroke, Satin, a green
// Color Overlay and a yellow Outer Glow.
patchy::Document swept_effects_document() {
  patchy::Document document(240, 160, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background", solid_pixels(240, 160, patchy::PixelFormat::rgb8(), QColor(255, 255, 255)));
  patchy::Layer paint(document.allocate_layer_id(), "Paint Layer",
                      solid_pixels(80, 80, patchy::PixelFormat::rgba8(), QColor(220, 40, 40)));
  paint.set_bounds(patchy::Rect{80, 40, 80, 80});
  auto& style = paint.layer_style();
  patchy::LayerBevelEmboss bevel;
  bevel.enabled = true;
  style.bevels.push_back(bevel);
  patchy::LayerStroke stroke;
  stroke.enabled = true;
  stroke.size = 6.0F;
  stroke.position = patchy::LayerStrokePosition::Outside;
  stroke.color = patchy::RgbColor{20, 40, 220};
  stroke.opacity = 1.0F;
  style.strokes.push_back(stroke);
  patchy::LayerSatin satin;
  satin.enabled = true;
  style.satins.push_back(satin);
  patchy::LayerColorOverlay overlay;
  overlay.enabled = true;
  overlay.color = patchy::RgbColor{20, 200, 40};
  overlay.opacity = 1.0F;
  style.color_overlays.push_back(overlay);
  patchy::LayerOuterGlow glow;
  glow.enabled = true;
  glow.color = patchy::RgbColor{255, 210, 0};
  glow.opacity = 1.0F;
  glow.size = 24.0F;
  style.outer_glows.push_back(glow);
  const auto paint_id = paint.id();
  document.add_layer(std::move(paint));
  document.set_active_layer(paint_id);
  return document;
}

constexpr std::array<const char*, 5> kSweptEffectNames{"Bevel & Emboss", "Stroke", "Satin", "Color Overlay",
                                                       "Outer Glow"};

QToolButton* swept_effect_eye(QListWidget& list, const char* effect_name) {
  auto* row = list.itemWidget(require_layer_item(list, QStringLiteral("Paint Layer")));
  auto* entry = row != nullptr ? effect_entry_row(*row, QString::fromLatin1(effect_name)) : nullptr;
  return entry != nullptr ? entry->findChild<QToolButton*>(QStringLiteral("layerEffectVisibilityButton")) : nullptr;
}

// The five effects' `enabled` flags in the rows' order.
std::array<bool, 5> swept_effect_flags(const patchy::LayerStyle& style) {
  return {style.bevels.front().enabled, style.strokes.front().enabled, style.satins.front().enabled,
          style.color_overlays.front().enabled, style.outer_glows.front().enabled};
}

// The handlers defer past the row rebuild, and the rebuild deletes the old rows later.
void settle_effect_rows() {
  for (int pass = 0; pass < 3; ++pass) {
    QApplication::processEvents();
  }
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

// A press on `start`'s eye, one move to each of `crossed` in turn (a name several rows from
// the last is one fast move over the rows between) and a release on the last, all through
// Qt's window system: the pressed eye is destroyed by the first rebuild, and only real
// routing shows whether the rest of the gesture still arrives. `x_offset` shifts the moves
// out of the eye column.
void sweep_effect_eyes_like_a_user(patchy::ui::MainWindow& window, QListWidget& list, const char* start,
                                   std::initializer_list<const char*> crossed, int x_offset = 0) {
  auto* handle = window.windowHandle();
  auto* eye = swept_effect_eye(list, start);
  CHECK(handle != nullptr && eye != nullptr);
  if (handle == nullptr || eye == nullptr) {
    return;
  }
  auto point = eye->mapTo(&window, eye->rect().center());
  QTest::mousePress(handle, Qt::LeftButton, Qt::NoModifier, point);
  settle_effect_rows();
  for (const auto* name : crossed) {
    eye = swept_effect_eye(list, name);
    CHECK(eye != nullptr);
    if (eye == nullptr) {
      break;
    }
    point = eye->mapTo(&window, eye->rect().center()) + QPoint(x_offset, 0);
    QTest::mouseMove(handle, point);
    settle_effect_rows();
  }
  QTest::mouseRelease(handle, Qt::LeftButton, Qt::NoModifier, point);
  settle_effect_rows();
}

void ui_layer_effect_eye_sweep_sets_crossed_effects_to_first_state() {
  using Flags = std::array<bool, 5>;
  using patchy::ui::MainWindowTestAccess;
  patchy::ui::MainWindow window;
  show_window(window);
  // The seven rows need the Layers list's height, which the expanded Color panel takes.
  MainWindowTestAccess::panel_column(window)->set_panel_expanded(QStringLiteral("colorDock"), false);
  window.add_document_session(swept_effects_document(), QStringLiteral("Effect Eye Sweep"));
  QApplication::processEvents();
  auto* list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(list != nullptr);
  auto& doc = MainWindowTestAccess::document(window);
  const auto paint_id = *doc.active_layer_id();
  const auto style = [&]() -> const patchy::LayerStyle& {
    return std::as_const(doc).find_layer(paint_id)->layer_style();
  };
  const auto flags = [&] { return swept_effect_flags(style()); };
  const auto undo_depth = [&] { return MainWindowTestAccess::active_session_undo_depth(window); };
  expand_effects(*list, QStringLiteral("Paint Layer"));
  auto* row = list->itemWidget(require_layer_item(*list, QStringLiteral("Paint Layer")));
  CHECK(row != nullptr);
  QStringList names;
  for (auto* entry : row->findChildren<QWidget*>(QStringLiteral("layerEffectEntryRow"))) {
    names.push_back(entry->findChild<QLabel*>(QStringLiteral("layerEffectEntryLabel"))->text());
  }
  QStringList expected_names;
  for (const auto* name : kSweptEffectNames) {
    expected_names.push_back(QString::fromLatin1(name));
  }
  CHECK(names == expected_names);
  for (const auto* name : kSweptEffectNames) {
    auto* eye = swept_effect_eye(*list, name);
    CHECK(eye != nullptr && eye->isVisible() && list->viewport()->rect().contains(
                                                    list->viewport()->mapFromGlobal(eye->mapToGlobal(eye->rect().center()))));
  }

  // The canvas with every effect on: a blue stroke ring, a green square, a glow past the ring.
  const QPoint ring(76, 80);
  const QPoint center(120, 80);
  const QPoint halo(66, 80);
  const auto ring_on = composite_pixel(window, ring.x(), ring.y());
  const auto center_on = composite_pixel(window, center.x(), center.y());
  const auto halo_on = composite_pixel(window, halo.x(), halo.y());
  CHECK(ring_on.blue() > 150 && ring_on.red() < 100);
  CHECK(center_on.green() > center_on.red() + 40);
  CHECK(halo_on.blue() < 235);
  const auto selected_before = list->selectedItems().size();
  const auto current_before = list->currentItem()->data(Qt::UserRole).toULongLong();

  // Down: press Bevel & Emboss and move straight to Satin. The Stroke row between them is
  // crossed by that one move. Three effects hide, the two below stay, in one undo step.
  auto undo_before = undo_depth();
  QPointer<QToolButton> pressed_eye = swept_effect_eye(*list, "Bevel & Emboss");
  sweep_effect_eyes_like_a_user(window, *list, "Bevel & Emboss", {"Satin"});
  CHECK(pressed_eye.isNull());  // the rows were rebuilt under the gesture
  CHECK(QWidget::mouseGrabber() == nullptr);
  CHECK((flags() == Flags{false, false, false, true, true}));
  CHECK(undo_depth() == undo_before + 1);
  CHECK(MainWindowTestAccess::current_history_label(window) == QStringLiteral("Hide Effects"));
  CHECK(!swept_effect_eye(*list, "Stroke")->isChecked());
  CHECK(swept_effect_eye(*list, "Color Overlay")->isChecked());
  const auto ring_hidden = composite_pixel(window, ring.x(), ring.y());
  CHECK(!(ring_hidden.blue() > 150 && ring_hidden.red() < 100));  // the stroke is gone
  const auto center_kept = composite_pixel(window, center.x(), center.y());
  CHECK(center_kept.green() > center_kept.red() + 40);  // the overlay still greens it
  CHECK(style().effects_visible);
  CHECK(std::as_const(doc).find_layer(paint_id)->visible());
  // The press-drag selects nothing and starts no row drag.
  CHECK(list->selectedItems().size() == selected_before);
  CHECK(list->currentItem()->data(Qt::UserRole).toULongLong() == current_before);
  // A plain hover after the release changes nothing.
  QTest::mouseMove(window.windowHandle(), swept_effect_eye(*list, "Outer Glow")->mapTo(
                                              &window, swept_effect_eye(*list, "Outer Glow")->rect().center()));
  settle_effect_rows();
  CHECK((flags() == Flags{false, false, false, true, true}));

  // One undo brings all three back; redo hides them again.
  MainWindowTestAccess::undo(window);
  settle_effect_rows();
  CHECK((flags() == Flags{true, true, true, true, true}));
  MainWindowTestAccess::redo(window);
  settle_effect_rows();
  CHECK((flags() == Flags{false, false, false, true, true}));

  // Up, across mixed states, with backtracking: press Outer Glow (visible, so the sweep
  // hides), cross Color Overlay, reach Satin (hidden already, left as it is), then go back
  // down over the two just hidden. Nothing flips back; the two changes are one undo step.
  undo_before = undo_depth();
  sweep_effect_eyes_like_a_user(window, *list, "Outer Glow",
                                {"Color Overlay", "Satin", "Color Overlay", "Outer Glow", "Color Overlay"});
  CHECK((flags() == Flags{false, false, false, false, false}));
  CHECK(undo_depth() == undo_before + 1);
  CHECK(MainWindowTestAccess::current_history_label(window) == QStringLiteral("Hide Effects"));
  const auto center_bare = composite_pixel(window, center.x(), center.y());
  CHECK(center_bare.red() > 150 && center_bare.green() < 100);  // the square's own red
  const auto halo_bare = composite_pixel(window, halo.x(), halo.y());
  CHECK(halo_bare.red() > 250 && halo_bare.green() > 250 && halo_bare.blue() > 250);

  // Up from the last empty slot to the first in one move: every effect shows again and
  // the canvas is what it was.
  undo_before = undo_depth();
  sweep_effect_eyes_like_a_user(window, *list, "Outer Glow", {"Bevel & Emboss"});
  CHECK((flags() == Flags{true, true, true, true, true}));
  CHECK(undo_depth() == undo_before + 1);
  CHECK(MainWindowTestAccess::current_history_label(window) == QStringLiteral("Show Effects"));
  CHECK(composite_pixel(window, ring.x(), ring.y()) == ring_on);
  CHECK(composite_pixel(window, center.x(), center.y()) == center_on);
  CHECK(composite_pixel(window, halo.x(), halo.y()) == halo_on);

  // A drag that leaves the eye column stops there: only the pressed effect changes, and
  // its step keeps the effect's own name.
  undo_before = undo_depth();
  sweep_effect_eyes_like_a_user(window, *list, "Bevel & Emboss", {"Stroke", "Satin"}, /*x_offset=*/60);
  CHECK((flags() == Flags{false, true, true, true, true}));
  CHECK(undo_depth() == undo_before + 1);
  CHECK(MainWindowTestAccess::current_history_label(window) == QStringLiteral("Hide Bevel & Emboss"));
  MainWindowTestAccess::undo(window);
  settle_effect_rows();

  // Up past the first effect: the Layer Styles master eye shares the column and the layer's
  // own row lies beyond it. Neither is an effect eye, so neither changes.
  {
    auto* handle = window.windowHandle();
    auto* eye = swept_effect_eye(*list, "Stroke");
    QTest::mousePress(handle, Qt::LeftButton, Qt::NoModifier, eye->mapTo(&window, eye->rect().center()));
    settle_effect_rows();
    row = list->itemWidget(require_layer_item(*list, QStringLiteral("Paint Layer")));
    auto* master = row->findChild<QToolButton*>(QStringLiteral("layerEffectsVisibilityButton"));
    CHECK(master != nullptr);
    const auto master_point = master->mapTo(&window, master->rect().center());
    QTest::mouseMove(handle, master_point);
    settle_effect_rows();
    const auto layer_row_point = QPoint(master_point.x(), master_point.y() - 30);
    QTest::mouseMove(handle, layer_row_point);
    settle_effect_rows();
    QTest::mouseRelease(handle, Qt::LeftButton, Qt::NoModifier, layer_row_point);
    settle_effect_rows();
    CHECK((flags() == Flags{false, false, true, true, true}));
    CHECK(style().effects_visible);
    CHECK(std::as_const(doc).find_layer(paint_id)->visible());
    CHECK(QWidget::mouseGrabber() == nullptr);
    MainWindowTestAccess::undo(window);
    settle_effect_rows();
    CHECK((flags() == Flags{true, true, true, true, true}));
  }

  // A plain click still toggles one effect, one undo step each, and so does each click of a
  // quick pair (the second arrives as a double-click).
  undo_before = undo_depth();
  click_row_child(*list, *swept_effect_eye(*list, "Stroke"));
  CHECK((flags() == Flags{true, false, true, true, true}));
  CHECK(undo_depth() == undo_before + 1);
  CHECK(MainWindowTestAccess::current_history_label(window) == QStringLiteral("Hide Stroke"));
  click_row_child(*list, *swept_effect_eye(*list, "Stroke"));
  CHECK((flags() == Flags{true, true, true, true, true}));
  CHECK(undo_depth() == undo_before + 2);
  {
    // The press rebuilds the rows and deletes the eye: take its points first.
    auto* eye = swept_effect_eye(*list, "Satin");
    const auto eye_center = eye->rect().center();
    const auto release_point = list->viewport()->mapFromGlobal(eye->mapToGlobal(eye_center));
    send_mouse(*eye, QEvent::MouseButtonPress, eye_center, Qt::LeftButton, Qt::LeftButton);
    send_mouse(*list->viewport(), QEvent::MouseButtonRelease, release_point, Qt::LeftButton, Qt::NoButton);
    settle_effect_rows();
    CHECK((flags() == Flags{true, true, false, true, true}));
    eye = swept_effect_eye(*list, "Satin");
    send_mouse(*eye, QEvent::MouseButtonDblClick, eye_center, Qt::LeftButton, Qt::LeftButton);
    send_mouse(*list->viewport(), QEvent::MouseButtonRelease, release_point, Qt::LeftButton, Qt::NoButton);
    settle_effect_rows();
    CHECK((flags() == Flags{true, true, true, true, true}));
    CHECK(undo_depth() == undo_before + 4);
  }

  // The master eye still hides and shows the whole list with a click; a locked layer's
  // effect eyes change nothing.
  row = list->itemWidget(require_layer_item(*list, QStringLiteral("Paint Layer")));
  click_row_child(*list, *row->findChild<QToolButton*>(QStringLiteral("layerEffectsVisibilityButton")));
  CHECK(!style().effects_visible);
  CHECK((flags() == Flags{true, true, true, true, true}));
  row = list->itemWidget(require_layer_item(*list, QStringLiteral("Paint Layer")));
  click_row_child(*list, *row->findChild<QToolButton*>(QStringLiteral("layerEffectsVisibilityButton")));
  CHECK(style().effects_visible);
  patchy::set_layer_lock_flags(*doc.find_layer(paint_id), patchy::kLayerLockAll);
  MainWindowTestAccess::refresh_layer_ui(window);
  settle_effect_rows();
  CHECK(swept_effect_eye(*list, "Bevel & Emboss") != nullptr && !swept_effect_eye(*list, "Bevel & Emboss")->isEnabled());
  undo_before = undo_depth();
  click_row_child(*list, *swept_effect_eye(*list, "Bevel & Emboss"));
  CHECK((flags() == Flags{true, true, true, true, true}));
  CHECK(undo_depth() == undo_before);
  CHECK(QWidget::mouseGrabber() == nullptr);
}

// The moves of one sweep delivered in a burst (Wayland hands over every motion): the sweep
// keeps each one, since crossing is a matter of the path, and ends where the same moves
// sent one by one leave it, as one undo step.
void ui_layer_effect_eye_sweep_handles_queued_moves() {
  using Flags = std::array<bool, 5>;
  using patchy::ui::MainWindowTestAccess;
  patchy::ui::MainWindow window;
  show_window(window);
  MainWindowTestAccess::panel_column(window)->set_panel_expanded(QStringLiteral("colorDock"), false);
  window.add_document_session(swept_effects_document(), QStringLiteral("Effect Eye Sweep Queue"));
  QApplication::processEvents();
  auto* list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(list != nullptr);
  auto* viewport = list->viewport();
  auto& doc = MainWindowTestAccess::document(window);
  const auto paint_id = *doc.active_layer_id();
  const auto flags = [&] { return swept_effect_flags(std::as_const(doc).find_layer(paint_id)->layer_style()); };
  const auto undo_depth = [&] { return MainWindowTestAccess::active_session_undo_depth(window); };
  expand_effects(*list, QStringLiteral("Paint Layer"));
  const auto eye_point = [&](const char* name) {
    auto* eye = swept_effect_eye(*list, name);
    CHECK(eye != nullptr);
    return eye != nullptr ? viewport->mapFromGlobal(eye->mapToGlobal(eye->rect().center())) : QPoint();
  };
  const auto press = [&](const char* name) {
    auto* eye = swept_effect_eye(*list, name);
    CHECK(eye != nullptr);
    send_mouse(*eye, QEvent::MouseButtonPress, eye->rect().center(), Qt::LeftButton, Qt::LeftButton);
    settle_effect_rows();
  };
  // 30 moves from `from` to `to`, evenly spaced.
  const auto path = [](QPoint from, QPoint to) {
    std::vector<QPoint> points;
    for (int step = 1; step <= 30; ++step) {
      points.push_back(from + (to - from) * step / 30);
    }
    return points;
  };
  const auto post_moves = [&](const std::vector<QPoint>& points) {
    for (const auto point : points) {
      QApplication::postEvent(viewport, new QMouseEvent(QEvent::MouseMove, QPointF(point),
                                                        QPointF(viewport->mapToGlobal(point)), Qt::NoButton,
                                                        Qt::LeftButton, Qt::NoModifier));
    }
  };
  const auto top = eye_point("Bevel & Emboss");
  const auto bottom = eye_point("Outer Glow");
  CHECK(bottom.y() > top.y() + 80);

  // Queued: the whole way down in one burst, then up and down again in a second burst.
  const auto undo_before = undo_depth();
  press("Bevel & Emboss");
  CHECK((flags() == Flags{false, true, true, true, true}));
  post_moves(path(top, bottom));
  settle_effect_rows();
  settle_effect_rows();
  CHECK((flags() == Flags{false, false, false, false, false}));
  CHECK(undo_depth() == undo_before + 1);
  auto back_and_forth = path(bottom, top);
  const auto down_again = path(top, bottom);
  back_and_forth.insert(back_and_forth.end(), down_again.begin(), down_again.end());
  post_moves(back_and_forth);
  settle_effect_rows();
  settle_effect_rows();
  CHECK((flags() == Flags{false, false, false, false, false}));  // backtracking flips nothing
  CHECK(undo_depth() == undo_before + 1);
  send_mouse(*viewport, QEvent::MouseButtonRelease, bottom, Qt::LeftButton, Qt::NoButton);
  settle_effect_rows();
  CHECK(QWidget::mouseGrabber() == nullptr);
  CHECK(MainWindowTestAccess::current_history_label(window) == QStringLiteral("Hide Effects"));
  const auto queued = flags();
  // Moves after the release belong to no sweep.
  post_moves(path(bottom, top));
  settle_effect_rows();
  CHECK(flags() == queued);

  // One undo step for the burst; the same moves one by one end in the same place.
  MainWindowTestAccess::undo(window);
  settle_effect_rows();
  CHECK((flags() == Flags{true, true, true, true, true}));
  CHECK(undo_depth() == undo_before);
  press("Bevel & Emboss");
  for (const auto point : path(top, bottom)) {
    send_mouse(*viewport, QEvent::MouseMove, point, Qt::NoButton, Qt::LeftButton);
  }
  settle_effect_rows();
  send_mouse(*viewport, QEvent::MouseButtonRelease, bottom, Qt::LeftButton, Qt::NoButton);
  settle_effect_rows();
  CHECK(flags() == queued);
  CHECK(undo_depth() == undo_before + 1);
}

// local-063: an effect eye changes which effects are drawn, never what an effect's mask is
// made from, so the frame after a click computes no mask anew and is still, pixel for pixel,
// what a render with no cached mask draws.
//
// A white 400x300 document with a red 120x100 "Paint Layer" at (140,100) whose effects all
// stay inside the canvas (their masks are then cacheable): Bevel & Emboss, a 4 px outside
// Stroke, Inner Glow, Satin and a Drop Shadow. `stroke_emboss` swaps in a Stroke Emboss
// bevel over two strokes, the one mask that reads other effects' switches.
patchy::Document mask_reuse_document(bool stroke_emboss) {
  patchy::Document document(400, 300, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background", solid_pixels(400, 300, patchy::PixelFormat::rgb8(), QColor(255, 255, 255)));
  patchy::Layer paint(document.allocate_layer_id(), "Paint Layer",
                      solid_pixels(120, 100, patchy::PixelFormat::rgba8(), QColor(220, 40, 40)));
  paint.set_bounds(patchy::Rect{140, 100, 120, 100});
  auto& style = paint.layer_style();
  patchy::LayerBevelEmboss bevel;
  bevel.enabled = true;
  bevel.size = 8.0F;
  if (stroke_emboss) {
    bevel.style = patchy::BevelEmbossStyleKind::StrokeEmboss;
  }
  style.bevels.push_back(bevel);
  patchy::LayerStroke stroke;
  stroke.enabled = true;
  stroke.size = stroke_emboss ? 10.0F : 4.0F;
  stroke.position = patchy::LayerStrokePosition::Outside;
  stroke.color = patchy::RgbColor{20, 40, 220};
  stroke.opacity = 1.0F;
  style.strokes.push_back(stroke);
  if (stroke_emboss) {
    stroke.size = 4.0F;
    stroke.color = patchy::RgbColor{240, 200, 40};
    style.strokes.push_back(stroke);
    const auto paint_id = paint.id();
    document.add_layer(std::move(paint));
    document.set_active_layer(paint_id);
    return document;
  }
  patchy::LayerInnerGlow inner_glow;
  inner_glow.enabled = true;
  inner_glow.size = 12.0F;
  style.inner_glows.push_back(inner_glow);
  patchy::LayerSatin satin;
  satin.enabled = true;
  style.satins.push_back(satin);
  patchy::LayerDropShadow shadow;
  shadow.enabled = true;
  shadow.distance = 10.0F;
  shadow.size = 8.0F;
  style.drop_shadows.push_back(shadow);
  const auto paint_id = paint.id();
  document.add_layer(std::move(paint));
  document.set_active_layer(paint_id);
  return document;
}

// The frame the canvas shows once its render has settled.
QImage settled_canvas_frame(patchy::ui::MainWindow& window) {
  auto* canvas = require_canvas(window);
  QImage frame;
  CHECK(process_events_until([&] {
    frame = canvas->current_composite();
    return canvas->render_settled() && !frame.isNull();
  }));
  return frame.convertToFormat(QImage::Format_RGBA8888);
}

// The document rendered with no cached mask to lean on: the copy's style accessor gives
// its layer new revisions, so every mask is computed anew.
QImage cold_document_render(const patchy::Document& document, patchy::LayerId styled_layer) {
  auto copy = document;
  static_cast<void>(copy.find_layer(styled_layer)->layer_style());
  return patchy::ui::qimage_from_document(std::as_const(copy), true).convertToFormat(QImage::Format_RGBA8888);
}

void ui_layer_effect_eye_toggle_reuses_style_masks_and_matches_cold_render() {
  using patchy::ui::MainWindowTestAccess;
  using patchy::ui::style_mask_cache_misses;
  patchy::ui::MainWindow window;
  show_window(window);
  MainWindowTestAccess::panel_column(window)->set_panel_expanded(QStringLiteral("colorDock"), false);
  const auto misses_at_start = style_mask_cache_misses();
  window.add_document_session(mask_reuse_document(false), QStringLiteral("Effect Eye Masks"));
  QApplication::processEvents();
  auto* list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(list != nullptr);
  auto& doc = MainWindowTestAccess::document(window);
  const auto paint_id = *doc.active_layer_id();
  const auto style = [&]() -> const patchy::LayerStyle& {
    return std::as_const(doc).find_layer(paint_id)->layer_style();
  };
  expand_effects(*list, QStringLiteral("Paint Layer"));
  const std::array<const char*, 5> names{"Bevel & Emboss", "Stroke", "Inner Glow", "Satin", "Drop Shadow"};
  for (const auto* name : names) {
    CHECK(swept_effect_eye(*list, name) != nullptr);
  }

  // The first frame computed this layer's masks through the cache (the checks below would
  // pass emptily otherwise), and it is what a cold render draws.
  const auto first_frame = settled_canvas_frame(window);
  CHECK(style_mask_cache_misses() > misses_at_start);
  CHECK(first_frame == cold_document_render(doc, paint_id));
  // A cold render is counted, so the counter does see this document's masks.
  const auto misses_before_cold = style_mask_cache_misses();
  static_cast<void>(cold_document_render(doc, paint_id));
  CHECK(style_mask_cache_misses() > misses_before_cold);

  // Each eye off and on again: the frame is exact both times and no mask is computed, the
  // returning effect's own included.
  const auto frame_is_exact_and_reused = [&](std::uint64_t misses_before) {
    const auto frame = settled_canvas_frame(window);
    const auto misses_after = style_mask_cache_misses();
    if (misses_after != misses_before) {
      std::cout << "  the frame computed " << (misses_after - misses_before) << " mask(s)\n";
    }
    CHECK(misses_after == misses_before);
    CHECK(frame == cold_document_render(doc, paint_id));
    return frame;
  };
  for (const auto* name : names) {
    auto misses = style_mask_cache_misses();
    click_row_child(*list, *swept_effect_eye(*list, name));
    const auto hidden = frame_is_exact_and_reused(misses);
    CHECK(hidden != first_frame);  // the effect did leave the canvas
    misses = style_mask_cache_misses();
    click_row_child(*list, *swept_effect_eye(*list, name));
    CHECK(frame_is_exact_and_reused(misses) == first_frame);
  }

  // The Layer Styles eye hides and shows the whole list the same way.
  auto misses = style_mask_cache_misses();
  auto* row = list->itemWidget(require_layer_item(*list, QStringLiteral("Paint Layer")));
  click_row_child(*list, *row->findChild<QToolButton*>(QStringLiteral("layerEffectsVisibilityButton")));
  CHECK(!style().effects_visible);
  static_cast<void>(frame_is_exact_and_reused(misses));
  misses = style_mask_cache_misses();
  row = list->itemWidget(require_layer_item(*list, QStringLiteral("Paint Layer")));
  click_row_child(*list, *row->findChild<QToolButton*>(QStringLiteral("layerEffectsVisibilityButton")));
  CHECK(style().effects_visible);
  CHECK(frame_is_exact_and_reused(misses) == first_frame);

  // A sweep down every eye (one undo step), its undo and its redo.
  misses = style_mask_cache_misses();
  const auto undo_before = MainWindowTestAccess::active_session_undo_depth(window);
  sweep_effect_eyes_like_a_user(window, *list, "Bevel & Emboss", {"Drop Shadow"});
  CHECK(!style().bevels.front().enabled && !style().strokes.front().enabled && !style().inner_glows.front().enabled &&
        !style().satins.front().enabled && !style().drop_shadows.front().enabled);
  CHECK(MainWindowTestAccess::active_session_undo_depth(window) == undo_before + 1);
  const auto swept = frame_is_exact_and_reused(misses);
  misses = style_mask_cache_misses();
  MainWindowTestAccess::undo(window);
  settle_effect_rows();
  CHECK(style().bevels.front().enabled && style().drop_shadows.front().enabled);
  CHECK(frame_is_exact_and_reused(misses) == first_frame);
  misses = style_mask_cache_misses();
  MainWindowTestAccess::redo(window);
  settle_effect_rows();
  CHECK(frame_is_exact_and_reused(misses) == swept);
  MainWindowTestAccess::undo(window);
  settle_effect_rows();

  // Quick clicks with no frame awaited between them: the last state is the one drawn.
  misses = style_mask_cache_misses();
  for (const auto* name : {"Satin", "Stroke", "Satin", "Bevel & Emboss", "Stroke", "Drop Shadow"}) {
    auto* eye = swept_effect_eye(*list, name);
    CHECK(eye != nullptr);
    const auto global = eye->mapToGlobal(eye->rect().center());
    click_widget_like_a_user(*list->viewport(), list->viewport()->mapFromGlobal(global));
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  }
  CHECK(style().satins.front().enabled && style().strokes.front().enabled && !style().bevels.front().enabled &&
        !style().drop_shadows.front().enabled && style().inner_glows.front().enabled);
  static_cast<void>(frame_is_exact_and_reused(misses));
}

// The exception: a Stroke Emboss bevel's height field is built from the strokes that are on,
// so hiding or showing a stroke must compute that mask again. The frame stays exact.
void ui_layer_effect_eye_toggle_recomputes_stroke_emboss_bevel() {
  using patchy::ui::MainWindowTestAccess;
  using patchy::ui::style_mask_cache_misses;
  patchy::ui::MainWindow window;
  show_window(window);
  MainWindowTestAccess::panel_column(window)->set_panel_expanded(QStringLiteral("colorDock"), false);
  window.add_document_session(mask_reuse_document(true), QStringLiteral("Effect Eye Stroke Emboss"));
  QApplication::processEvents();
  auto* list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(list != nullptr);
  auto& doc = MainWindowTestAccess::document(window);
  const auto paint_id = *doc.active_layer_id();
  const auto style = [&]() -> const patchy::LayerStyle& {
    return std::as_const(doc).find_layer(paint_id)->layer_style();
  };
  expand_effects(*list, QStringLiteral("Paint Layer"));
  const auto stroke_eye = [&](int which) -> QToolButton* {
    auto* row = list->itemWidget(require_layer_item(*list, QStringLiteral("Paint Layer")));
    QList<QToolButton*> eyes;
    for (auto* entry : row->findChildren<QWidget*>(QStringLiteral("layerEffectEntryRow"))) {
      auto* label = entry->findChild<QLabel*>(QStringLiteral("layerEffectEntryLabel"));
      if (label != nullptr && label->text() == QStringLiteral("Stroke")) {
        eyes.push_back(entry->findChild<QToolButton*>(QStringLiteral("layerEffectVisibilityButton")));
      }
    }
    return which < eyes.size() ? eyes[which] : nullptr;
  };
  CHECK(stroke_eye(0) != nullptr && stroke_eye(1) != nullptr);
  const auto first_frame = settled_canvas_frame(window);
  CHECK(first_frame == cold_document_render(doc, paint_id));

  // The wide stroke off, on, then the narrow one off and on: every frame exact, and each
  // one computed the bevel's mask again.
  for (const int which : {0, 0, 1, 1}) {
    const auto misses = style_mask_cache_misses();
    click_row_child(*list, *stroke_eye(which));
    const auto frame = settled_canvas_frame(window);
    CHECK(style_mask_cache_misses() > misses);
    CHECK(frame == cold_document_render(doc, paint_id));
  }
  CHECK(style().strokes[0].enabled && style().strokes[1].enabled);
  CHECK(settled_canvas_frame(window) == first_frame);

  // The bevel's own eye changes no stroke, yet its mask is still left behind (the rule is
  // by layer, not by which eye was clicked); the frames stay exact.
  click_row_child(*list, *swept_effect_eye(*list, "Bevel & Emboss"));
  CHECK(!style().bevels.front().enabled);
  CHECK(settled_canvas_frame(window) == cold_document_render(doc, paint_id));
  click_row_child(*list, *swept_effect_eye(*list, "Bevel & Emboss"));
  CHECK(settled_canvas_frame(window) == first_frame);
}

// local-063 pictures (Dark): the window with every effect on, then after one press on the
// Bevel & Emboss eye dragged down the eye column to Outer Glow. No checks on the outcome,
// so the same run on the code before the change gives the BEFORE picture.
void ui_layer_effect_eye_sweep_local_063_shots() {
  ColorSchemeRestorer restore_scheme;
  ColorSchemeRestorer::apply(patchy::ui::ColorSchemePreference::Dark);
  patchy::ui::MainWindow window;
  show_window(window);
  patchy::ui::MainWindowTestAccess::panel_column(window)->set_panel_expanded(QStringLiteral("colorDock"), false);
  window.add_document_session(swept_effects_document(), QStringLiteral("Effect Eye Sweep"));
  QApplication::processEvents();
  auto* list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(list != nullptr);
  expand_effects(*list, QStringLiteral("Paint Layer"));
  auto* canvas = require_canvas(window);
  canvas->force_refresh();
  QApplication::processEvents();
  save_widget_artifact("ui_layer_effect_eye_sweep_local_063_start", window);
  sweep_effect_eyes_like_a_user(window, *list, "Bevel & Emboss", {"Stroke", "Satin", "Color Overlay", "Outer Glow"});
  CHECK(process_events_until([&] { return !canvas->current_composite().isNull(); }));
  canvas->force_refresh();
  QApplication::processEvents();
  save_widget_artifact("ui_layer_effect_eye_sweep_local_063_swept", window);
}

}  // namespace

std::vector<patchy::test::TestCase> layer_effect_rows_tests() {
  return {
      {"ui_layer_effect_eye_sweep_sets_crossed_effects_to_first_state",
       ui_layer_effect_eye_sweep_sets_crossed_effects_to_first_state},
      {"ui_layer_effect_eye_sweep_handles_queued_moves", ui_layer_effect_eye_sweep_handles_queued_moves},
      {"ui_layer_effect_eye_sweep_local_063_shots", ui_layer_effect_eye_sweep_local_063_shots},
      {"ui_layer_effect_eye_toggle_reuses_style_masks_and_matches_cold_render",
       ui_layer_effect_eye_toggle_reuses_style_masks_and_matches_cold_render},
      {"ui_layer_effect_eye_toggle_recomputes_stroke_emboss_bevel",
       ui_layer_effect_eye_toggle_recomputes_stroke_emboss_bevel},
      {"ui_layer_rows_with_every_badge_fit_narrow_and_wide", ui_layer_rows_with_every_badge_fit_narrow_and_wide},
      {"ui_layer_advanced_blending_badge_names_settings_and_opens_blending_options",
       ui_layer_advanced_blending_badge_names_settings_and_opens_blending_options},
      {"ui_layer_fx_badge_right_click_opens_style_menu", ui_layer_fx_badge_right_click_opens_style_menu},
      {"ui_layer_effect_rows_start_collapsed_and_remember_state", ui_layer_effect_rows_start_collapsed_and_remember_state},
      {"ui_layer_effect_rows_grey_eyes_under_master_off", ui_layer_effect_rows_grey_eyes_under_master_off},
      {"ui_layer_effect_rows_align_under_the_layer_styles_eye", ui_layer_effect_rows_align_under_the_layer_styles_eye},
      {"ui_layer_smart_filter_eyes_align_with_layer_style_eyes", ui_layer_smart_filter_eyes_align_with_layer_style_eyes},
      {"ui_layer_smart_filter_rows_collapse_and_save_state", ui_layer_smart_filter_rows_collapse_and_save_state},
      {"ui_layer_effect_row_context_menu_edits_hides_and_deletes",
       ui_layer_effect_row_context_menu_edits_hides_and_deletes},
      {"ui_layer_effect_rows_toggle_effects_and_render", ui_layer_effect_rows_toggle_effects_and_render},
      {"ui_layer_effect_row_double_click_and_pencil_open_dialog_on_effect",
       ui_layer_effect_row_double_click_and_pencil_open_dialog_on_effect},
  };
}
