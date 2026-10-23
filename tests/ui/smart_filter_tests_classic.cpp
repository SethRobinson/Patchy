// Photoshop's parameterless Smart Filters (Blur, Blur More, Sharpen, Sharpen
// More) in the Layers panel: their rows name the filter and their settings
// dialog holds only the Blending section. Appended after the historical
// smart_filter_tests parts so the earlier registration order is unchanged.

#include "core/document.hpp"
#include "core/smart_filter.hpp"
#include "core/smart_filter_effects.hpp"
#include "core/smart_object.hpp"
#include "ui/main_window.hpp"
#include "ui/app_settings.hpp"
#include "ui/smart_object_render.hpp"
#include "psd/psd_document_io.hpp"
#include "test_harness.hpp"
#include "local_psd_fixtures.hpp"
#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QPainter>
#include <QPixmap>
#include <QSpinBox>
#include <QTimer>
#include <QToolButton>
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <utility>
#include <vector>
#include "ui_test_access.hpp"
#include "ui_test_groups.hpp"
#include "ui_test_support.hpp"
#include "smart_filter_test_support.hpp"

namespace {

using namespace patchy::test::ui;

void ui_smart_filter_parameterless_entry_edits_blending_only() {
  SettingsValueRestorer notes_setting(
      QStringLiteral("imports/showPsdWarningsAndInfo"));
  patchy::ui::app_settings().remove(
      QStringLiteral("imports/showPsdWarningsAndInfo"));
  patchy::ui::MainWindow window;
  show_window(window);
  const auto path = QString::fromStdWString(
      patchy::test::committed_psd_fixture_path(
          "photoshop-smart-filter-classic.psd")
          .wstring());
  patchy::ui::MainWindowTestAccess::open_document_path(window, path);
  QApplication::processEvents();

  const auto layer_name = QStringLiteral("Blur More");
  const auto layer_id = select_named_layer(window, layer_name);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  auto* layers = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layers != nullptr);
  layers->scrollToItem(require_layer_item(*layers, layer_name),
                       QAbstractItemView::PositionAtCenter);
  QApplication::processEvents();
  const auto current_entry = [&]() -> const patchy::SmartFilterEntry& {
    const auto* layer = std::as_const(document).find_layer(layer_id);
    CHECK(layer != nullptr && layer->smart_filter_stack() != nullptr);
    CHECK(layer->smart_filter_stack()->entries.size() == 1U);
    return layer->smart_filter_stack()->entries.front();
  };
  CHECK(current_entry().kind == patchy::SmartFilterKind::BlurMore);
  CHECK(current_entry().opacity == 1.0);

  auto* row = layers->itemWidget(require_layer_item(*layers, layer_name));
  CHECK(row != nullptr);
  bool labelled = false;
  for (auto* label : row->findChildren<QLabel*>(
           QStringLiteral("layerSmartFilterEntryLabel"))) {
    labelled = labelled || label->text() == QStringLiteral("Blur More");
  }
  CHECK(labelled);
  ensure_artifact_dir();
  layers->grab().save(
      QStringLiteral("test-artifacts/smart-filter-classic-layers.png"));
  auto* edit = row->findChild<QToolButton*>(
      QStringLiteral("layerSmartFilterEditButton"));
  CHECK(edit != nullptr && edit->isEnabled());

  const auto undo_before =
      patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);
  bool dialog_seen = false;
  QTimer::singleShot(20, [&] {
    auto* dialog = qobject_cast<QDialog*>(
        find_top_level_dialog(QStringLiteral("patchyFilterDialog")));
    CHECK(dialog != nullptr);
    // No filter parameters: the Blending section is the only content.
    CHECK(dialog->findChildren<QSpinBox*>().empty());
    auto* mode = dialog->findChild<QComboBox*>(
        QStringLiteral("smartFilterBlendModeCombo"));
    auto* opacity = dialog->findChild<QDoubleSpinBox*>(
        QStringLiteral("smartFilterOpacitySpin"));
    CHECK(mode != nullptr && opacity != nullptr);
    opacity->setValue(40.0);
    dialog_seen = true;
    dialog->accept();
  });
  edit->click();
  CHECK(process_events_until([&] {
    return std::abs(current_entry().opacity - 0.40) < 0.0000001;
  }));
  CHECK(dialog_seen);
  CHECK(current_entry().kind == patchy::SmartFilterKind::BlurMore);
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) ==
        undo_before + 1U);
  require_hotkey_action(window, QStringLiteral("edit.undo"))->trigger();
  CHECK(process_events_until([&] { return current_entry().opacity == 1.0; }));
}

void ui_smart_filter_parameterless_menu_command_appends_without_dialog() {
  // Filter > Blur > Blur More on a Smart Object appends Photoshop's native
  // entry at once, like Photoshop: no settings dialog, one undo step.
  SettingsValueRestorer notes_setting(
      QStringLiteral("imports/showPsdWarningsAndInfo"));
  patchy::ui::app_settings().remove(
      QStringLiteral("imports/showPsdWarningsAndInfo"));
  patchy::ui::MainWindow window;
  show_window(window);
  patchy::ui::MainWindowTestAccess::open_document_path(
      window, QString::fromStdWString(patchy::test::committed_psd_fixture_path(
                                          "photoshop-smart-filter-classic.psd")
                                          .wstring()));
  QApplication::processEvents();
  const auto layer_id = select_named_layer(window, QStringLiteral("_Source"));
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  CHECK(std::as_const(document).find_layer(layer_id)->smart_filter_stack() ==
        nullptr);
  const auto undo_before =
      patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);
  bool dialog_seen = false;
  QTimer::singleShot(50, [&] {
    if (auto* dialog = find_top_level_dialog(QStringLiteral("patchyFilterDialog"));
        dialog != nullptr) {
      dialog_seen = true;
      dialog->reject();
    }
  });
  auto* action = window.findChild<QAction*>(
      QStringLiteral("filterAction_patchy_filters_blur_more"));
  CHECK(action != nullptr && action->isEnabled());
  action->trigger();
  CHECK(process_events_until([&] {
    const auto* layer = std::as_const(document).find_layer(layer_id);
    return layer != nullptr && layer->smart_filter_stack() != nullptr;
  }));
  process_events_for(100);
  CHECK(!dialog_seen);
  const auto& stack =
      *std::as_const(document).find_layer(layer_id)->smart_filter_stack();
  CHECK(stack.entries.size() == 1U);
  CHECK(stack.entries.front().kind == patchy::SmartFilterKind::BlurMore);
  CHECK(!stack.entries.front().has_options);
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) ==
        undo_before + 1U);
}

void ui_smart_filter_canvas_unites_document_and_placed_bounds() {
  // Photoshop filters a Smart Object moved past the canvas edge inside the
  // document united with its placed bounds, reading the off-canvas pixels.
  // The production render path must match its baked pixels for an exact
  // filter of every family (October 2026 captures).
  struct Case {
    const char* fixture;
    const char* layer;
    // Gaussian Blur carries its own partial-alpha rounding tolerance
    // (docs/smart-filters-native.md); compare it premultiplied within 2.
    bool premultiplied_tolerance{false};
    // Median's RGB rule under partially transparent pixels is an open
    // calibration gap (docs/smart-filters-native.md); its alpha, which the
    // filter canvas decides, must match exactly.
    bool alpha_only{false};
  };
  const std::array<Case, 6> cases{{
      {"photoshop-smart-filter-classic.psd", "Blur More past left edge"},
      {"photoshop-smart-filter-past-edge.psd", "Gaussian Blur past left edge", true},
      {"photoshop-smart-filter-past-edge.psd", "Median past left edge", false, true},
      {"photoshop-smart-filter-past-edge.psd", "Find Edges past left edge"},
      {"photoshop-smart-filter-past-edge.psd", "Solarize past left edge"},
      {"photoshop-smart-filter-past-edge.psd", "Maximum past left edge"},
  }};
  for (const auto& test_case : cases) {
    const auto document = patchy::psd::DocumentIo::read_file(
        patchy::test::committed_psd_fixture_path(test_case.fixture));
    const patchy::Layer* layer = nullptr;
    for (const auto& candidate : document.layers()) {
      if (candidate.name() == test_case.layer) {
        layer = &candidate;
      }
    }
    CHECK(layer != nullptr && layer->smart_filter_stack() != nullptr);
    CHECK(layer->smart_filter_stack()->support ==
          patchy::SmartFilterStackSupport::Supported);
    CHECK(layer->bounds().x < 0);
    const auto preview = patchy::ui::render_smart_object_layer_preview(
        document, *layer, patchy::ui::CanvasWidget::TransformInterpolation::Bicubic);
    CHECK(preview.has_value());
    const auto& expected_bounds = layer->bounds();
    const auto& actual = preview->rendered;
    const auto bounds_equal = actual.bounds.x == expected_bounds.x &&
                              actual.bounds.y == expected_bounds.y &&
                              actual.bounds.width == expected_bounds.width &&
                              actual.bounds.height == expected_bounds.height;
    std::size_t mismatched = 0;
    if (bounds_equal) {
      for (std::int32_t y = 0; y < expected_bounds.height; ++y) {
        for (std::int32_t x = 0; x < expected_bounds.width; ++x) {
          const auto* want = layer->pixels().pixel(x, y);
          const auto* got = actual.pixels.pixel(x, y);
          if ((want[3] == 0U && got[3] == 0U) || std::equal(want, want + 4, got)) {
            continue;
          }
          bool within = (test_case.premultiplied_tolerance || test_case.alpha_only) &&
                        want[3] == got[3];
          for (std::size_t channel = 0;
               within && !test_case.alpha_only && channel < 3U; ++channel) {
            within = std::abs(int{want[channel]} - int{got[channel]}) * int{want[3]} <=
                     2 * 255;
          }
          mismatched += within ? 0U : 1U;
        }
      }
    }
    if (!bounds_equal || mismatched != 0U) {
      std::cerr << test_case.layer << ": bounds " << actual.bounds.x << ","
                << actual.bounds.width << " vs " << expected_bounds.x << ","
                << expected_bounds.width << ", " << mismatched
                << " pixels differ\n";
    }
    CHECK(bounds_equal);
    CHECK(mismatched == 0U);
  }
}

void ui_smart_filter_authored_cache_covers_the_canvas_beyond_the_document() {
  // A new stack on a Smart Object past the canvas edge writes its FEid cache
  // over the document united with the placed bounds (like Photoshop) while
  // the editable mask keeps the document rectangle. The saved file is the
  // Photoshop open-check artifact for that record shape.
  SettingsValueRestorer notes_setting(
      QStringLiteral("imports/showPsdWarningsAndInfo"));
  patchy::ui::app_settings().remove(
      QStringLiteral("imports/showPsdWarningsAndInfo"));
  patchy::ui::MainWindow window;
  show_window(window);
  patchy::ui::MainWindowTestAccess::open_document_path(
      window, QString::fromStdWString(patchy::test::committed_psd_fixture_path(
                                          "photoshop-smart-filter-classic.psd")
                                          .wstring()));
  QApplication::processEvents();
  const auto layer_name = QStringLiteral("Blur More past left edge");
  const auto layer_id = select_named_layer(window, layer_name);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  auto* layers = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layers != nullptr);
  auto* row = layers->itemWidget(require_layer_item(*layers, layer_name));
  CHECK(row != nullptr);
  auto* delete_all = row->findChild<QAction*>(
      QStringLiteral("layerSmartFiltersDeleteAction"));
  CHECK(delete_all != nullptr && delete_all->isEnabled());
  delete_all->trigger();
  CHECK(process_events_until([&] {
    return std::as_const(document).find_layer(layer_id)->smart_filter_stack() ==
           nullptr;
  }));
  auto* sharpen_more = window.findChild<QAction*>(
      QStringLiteral("filterAction_patchy_filters_sharpen_more"));
  CHECK(sharpen_more != nullptr);
  sharpen_more->trigger();
  CHECK(process_events_until([&] {
    return std::as_const(document).find_layer(layer_id)->smart_filter_stack() !=
           nullptr;
  }));
  const auto& layer = *std::as_const(document).find_layer(layer_id);
  const auto* record =
      std::as_const(document).metadata().smart_filter_effects.find_unique(
          patchy::smart_object_placed_uuid(layer));
  CHECK(record != nullptr && record->semantic_supported());
  CHECK(record->cache_bounds.x == -5 && record->cache_bounds.y == 0 &&
        record->cache_bounds.width == 53 && record->cache_bounds.height == 40);
  CHECK(record->mask.has_value() && record->mask->bounds.x == 0 &&
        record->mask->bounds.width == 48 && record->mask->bounds.height == 40);
  CHECK(layer.bounds().x < 0);

  ensure_artifact_dir();
  const auto artifact = std::filesystem::absolute(
      std::filesystem::path("test-artifacts") /
      "smart-filter-past-edge-authored.psd");
  patchy::psd::DocumentIo::write_layered_rgb8_file(document, artifact);
  const auto reopened = patchy::psd::DocumentIo::read_file(artifact);
  for (const auto& candidate : reopened.layers()) {
    if (candidate.name() == layer_name.toStdString()) {
      CHECK(candidate.smart_filter_stack() != nullptr);
      CHECK(candidate.smart_filter_stack()->support ==
            patchy::SmartFilterStackSupport::Supported);
      CHECK(candidate.smart_filter_stack()->entries.front().kind ==
            patchy::SmartFilterKind::SharpenMore);
    }
  }
}

void ui_smart_filter_sharpen_edges_menu_command_authors_native_entry() {
  // Filter > Sharpen > Sharpen Edges on a Smart Object appends Photoshop's
  // parameterless 'ShrE' entry without a dialog. The saved file is the
  // Photoshop open-check artifact for a Patchy-authored Sharpen Edges.
  SettingsValueRestorer notes_setting(
      QStringLiteral("imports/showPsdWarningsAndInfo"));
  patchy::ui::app_settings().remove(
      QStringLiteral("imports/showPsdWarningsAndInfo"));
  patchy::ui::MainWindow window;
  show_window(window);
  patchy::ui::MainWindowTestAccess::open_document_path(
      window, QString::fromStdWString(patchy::test::committed_psd_fixture_path(
                                          "photoshop-smart-filter-classic.psd")
                                          .wstring()));
  QApplication::processEvents();
  const auto layer_id = select_named_layer(window, QStringLiteral("_Source"));
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  CHECK(std::as_const(document).find_layer(layer_id)->smart_filter_stack() ==
        nullptr);
  auto* action = window.findChild<QAction*>(
      QStringLiteral("filterAction_patchy_filters_sharpen_edges"));
  CHECK(action != nullptr && action->isEnabled());
  // It sits in the Filter > Sharpen submenu, whose owner opts out of macOS
  // menu roles (docs/platform.md).
  bool in_submenu = false;
  for (auto* widget : action->associatedObjects()) {
    if (auto* menu = qobject_cast<QMenu*>(widget); menu != nullptr) {
      in_submenu = in_submenu || (menu->menuAction() != nullptr &&
                                  menu->menuAction()->menuRole() == QAction::NoRole);
    }
  }
  CHECK(in_submenu);
  action->trigger();
  CHECK(process_events_until([&] {
    const auto* layer = std::as_const(document).find_layer(layer_id);
    return layer != nullptr && layer->smart_filter_stack() != nullptr;
  }));
  const auto& stack =
      *std::as_const(document).find_layer(layer_id)->smart_filter_stack();
  CHECK(stack.entries.size() == 1U);
  CHECK(stack.entries.front().kind == patchy::SmartFilterKind::SharpenEdges);
  CHECK(stack.entries.front().native_filter_id == 0x53687245U);
  CHECK(!stack.entries.front().has_options);

  ensure_artifact_dir();
  const auto artifact = std::filesystem::absolute(
      std::filesystem::path("test-artifacts") / "sharpen-edges-authored.psd");
  patchy::psd::DocumentIo::write_layered_rgb8_file(document, artifact);
  const auto reopened = patchy::psd::DocumentIo::read_file(artifact);
  bool found = false;
  for (const auto& candidate : reopened.layers()) {
    if (candidate.name() == "_Source") {
      found = true;
      CHECK(candidate.smart_filter_stack() != nullptr);
      CHECK(candidate.smart_filter_stack()->support ==
            patchy::SmartFilterStackSupport::Supported);
      CHECK(candidate.smart_filter_stack()->entries.size() == 1U);
      CHECK(candidate.smart_filter_stack()->entries.front().kind ==
            patchy::SmartFilterKind::SharpenEdges);
    }
  }
  CHECK(found);
}

// Issue 347: a selector-less "background: transparent;" on a row label reached the
// label's tooltip through Qt's stylesheet cascade and cleared its ToolTipBase, so
// the translucent Windows 11 tip showed only its text. Every tooltip on the Smart
// Filter rows paints a full panel, like the layer row's own, in Dark and Light.
void ui_smart_filter_row_tooltips_paint_a_solid_panel() {
  SettingsValueRestorer notes_setting(
      QStringLiteral("imports/showPsdWarningsAndInfo"));
  patchy::ui::app_settings().remove(
      QStringLiteral("imports/showPsdWarningsAndInfo"));
  ColorSchemeRestorer restore_scheme;
  patchy::ui::MainWindow window;
  show_window(window);
  const auto path = QString::fromStdWString(
      patchy::test::committed_psd_fixture_path(
          "photoshop-smart-filter-classic.psd")
          .wstring());
  patchy::ui::MainWindowTestAccess::open_document_path(window, path);
  QApplication::processEvents();
  const auto layer_name = QStringLiteral("Blur More");
  (void)select_named_layer(window, layer_name);
  auto* layers = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  auto* layers_dock = window.findChild<QWidget*>(QStringLiteral("layersDock"));
  CHECK(layers != nullptr && layers_dock != nullptr);
  if (layers == nullptr || layers_dock == nullptr) {
    return;
  }

  for (const auto preference : {patchy::ui::ColorSchemePreference::Dark,
                                patchy::ui::ColorSchemePreference::Light}) {
    ColorSchemeRestorer::apply(preference);
    const bool light = preference == patchy::ui::ColorSchemePreference::Light;
    layers->scrollToItem(require_layer_item(*layers, layer_name),
                         QAbstractItemView::PositionAtCenter);
    QApplication::processEvents();
    auto* row = layers->itemWidget(require_layer_item(*layers, layer_name));
    CHECK(row != nullptr);
    if (row == nullptr) {
      return;
    }
    std::vector<QWidget*> anchors;
    int filter_anchors = 0;
    for (const auto& name : {QStringLiteral("layerMainRow"),
                             QStringLiteral("layerSmartFiltersRow"),
                             QStringLiteral("layerSmartFilterEntryRow")}) {
      for (auto* nested : row->findChildren<QWidget*>(name)) {
        auto widgets = nested->findChildren<QWidget*>();
        widgets.prepend(nested);
        for (auto* widget : widgets) {
          if (widget->isVisible() && !widget->toolTip().isEmpty()) {
            anchors.push_back(widget);
            filter_anchors += name == QStringLiteral("layerMainRow") ? 0 : 1;
          }
        }
      }
    }
    CHECK(filter_anchors >= 3);
    int unpainted = 0;
    std::vector<QWidget*> shown;
    for (auto* widget : anchors) {
      const auto tip = render_widget_tooltip(*widget);
      const auto share = unpainted_share(tip);
      if (tip.isNull() || share > 0.05) {
        ++unpainted;
        std::cout << "  " << (light ? "light" : "dark") << " tooltip of "
                  << widget->metaObject()->className() << "#"
                  << widget->objectName().toStdString() << " leaves "
                  << static_cast<int>(share * 100.0) << "% unpainted\n";
      }
      if (widget->objectName() == QStringLiteral("layerSmartFilterEntryLabel") ||
          widget->objectName() == QStringLiteral("layerSmartFilterMaskThumbnail")) {
        shown.push_back(widget);
      }
    }

    // The tips as the screen shows them: drawn over the panel without a window
    // fill, just below their control and inside the dock.
    CHECK(!shown.empty());
    for (auto* widget : shown) {
      const auto tip = render_widget_tooltip(*widget);
      auto shot = layers_dock->grab().toImage().convertToFormat(
          QImage::Format_ARGB32_Premultiplied);
      auto at = layers_dock->mapFromGlobal(
          widget->mapToGlobal(QPoint(8, widget->height() + 4)));
      at.setX(std::max(0, std::min(at.x(), shot.width() - tip.width() - 4)));
      const int bottom = std::min(shot.height(), at.y() + tip.height() + 12);
      {
        QPainter painter(&shot);
        painter.drawImage(at, tip);
      }
      ensure_artifact_dir();
      CHECK(shot.copy(0, 0, shot.width(), std::max(bottom, 1))
                .save(QStringLiteral("test-artifacts/%1-tooltip-%2.png")
                          .arg(widget->objectName(),
                               light ? QStringLiteral("light") : QStringLiteral("dark"))));
    }
    CHECK(unpainted == 0);
  }
}

}  // namespace

std::vector<patchy::test::TestCase> smart_filter_tests_classic() {
  return {
      {"ui_smart_filter_parameterless_entry_edits_blending_only",
       ui_smart_filter_parameterless_entry_edits_blending_only},
      {"ui_smart_filter_parameterless_menu_command_appends_without_dialog",
       ui_smart_filter_parameterless_menu_command_appends_without_dialog},
      {"ui_smart_filter_canvas_unites_document_and_placed_bounds",
       ui_smart_filter_canvas_unites_document_and_placed_bounds},
      {"ui_smart_filter_authored_cache_covers_the_canvas_beyond_the_document",
       ui_smart_filter_authored_cache_covers_the_canvas_beyond_the_document},
      {"ui_smart_filter_sharpen_edges_menu_command_authors_native_entry",
       ui_smart_filter_sharpen_edges_menu_command_authors_native_entry},
      {"ui_smart_filter_row_tooltips_paint_a_solid_panel",
       ui_smart_filter_row_tooltips_paint_a_solid_panel},
  };
}
