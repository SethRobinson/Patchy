// The Layers panel's effect rows (GitHub issue 256): a layer's effects listed under its
// row like Smart Filters, with an eye per effect, a master eye, and an Edit pencil or
// double-click that opens the Layer Style dialog on that effect.
#include "core/layer.hpp"
#include "core/layer_metadata.hpp"
#include "psd/psd_document_io.hpp"
#include "ui/canvas_widget.hpp"
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
#include <QStackedWidget>
#include <QDir>
#include <QFileInfo>
#include <QStatusBar>
#include <QTimer>
#include <QToolButton>
#include <QWidget>

#include <cmath>
#include <cstdint>
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

}  // namespace

std::vector<patchy::test::TestCase> layer_effect_rows_tests() {
  return {
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
