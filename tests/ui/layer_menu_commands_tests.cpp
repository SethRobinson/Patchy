// Layer selection stepping (#541), the Lock Layers dialog (#544), Layer > Export (#545) and
// Hide / Show Layers (#546): commands and default keys philipzae asked for.
#include "core/layer_metadata.hpp"
#include "ui/layer_lock_dialog.hpp"
#include "ui/main_window.hpp"
#include "test_harness.hpp"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QPushButton>
#include <QStatusBar>
#include <QToolButton>
#include <algorithm>
#include <array>
#include <string>
#include <utility>
#include <vector>
#include "ui_test_access.hpp"
#include "ui_test_groups.hpp"
#include "ui_test_support.hpp"

namespace {

using namespace patchy::test::ui;
using Access = patchy::ui::MainWindowTestAccess;

patchy::Layer square_layer(patchy::Document& doc, const std::string& name, QColor color) {
  patchy::Layer layer(doc.allocate_layer_id(), name, solid_pixels(40, 30, patchy::PixelFormat::rgba8(), color));
  layer.set_bounds(patchy::Rect{20, 20, 40, 30});
  return layer;
}

struct StackIds {
  patchy::LayerId background{};
  patchy::LayerId middle{};
  patchy::LayerId folder{};
  patchy::LayerId inner{};
  patchy::LayerId top{};
};

// Bottom to top: Background, Middle, Folder (Inner), Top.
StackIds add_stack(patchy::ui::MainWindow& window, const QString& title) {
  patchy::Document document(120, 90, patchy::PixelFormat::rgba8());
  StackIds ids;
  ids.background =
      document.add_pixel_layer("Background", solid_pixels(120, 90, patchy::PixelFormat::rgba8(), QColor(Qt::white)))
          .id();
  ids.middle = document.add_layer(square_layer(document, "Middle", QColor(220, 60, 50))).id();
  patchy::Layer folder(document.allocate_layer_id(), "Folder", patchy::LayerKind::Group);
  ids.folder = folder.id();
  auto inner = square_layer(document, "Inner", QColor(40, 160, 90));
  ids.inner = inner.id();
  folder.add_child(std::move(inner));
  document.add_layer(std::move(folder));
  ids.top = document.add_layer(square_layer(document, "Top", QColor(40, 90, 220))).id();
  document.set_active_layer(ids.top);
  window.add_document_session(std::move(document), title);
  show_window(window);
  QApplication::processEvents();
  return ids;
}

QListWidget* layer_list(patchy::ui::MainWindow& window) {
  auto* list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(list != nullptr);
  return list;
}

std::vector<patchy::LayerId> row_ids(QListWidget& list) {
  std::vector<patchy::LayerId> ids;
  for (int row = 0; row < list.count(); ++row) {
    ids.push_back(static_cast<patchy::LayerId>(list.item(row)->data(Qt::UserRole).toULongLong()));
  }
  return ids;
}

QMenu* menu_holding(patchy::ui::MainWindow& window, QAction* action) {
  for (auto* top : window.menuBar()->actions()) {
    if (top->menu() != nullptr && top->menu()->actions().contains(action)) {
      return top->menu();
    }
  }
  return nullptr;
}

// GitHub issue 541: Alt+[ / Alt+] step the selection one panel row down / up and stop at the
// ends; Alt+Shift adds the next row past the selection. A collapsed group is one row.
void ui_layer_select_below_above_steps_rows() {
  patchy::ui::MainWindow window;
  const auto ids = add_stack(window, QStringLiteral("Step Layers"));
  auto* list = layer_list(window);
  if (list == nullptr) {
    return;
  }
  auto* below = require_hotkey_action(window, QStringLiteral("select.layer_below"));
  auto* above = require_hotkey_action(window, QStringLiteral("select.layer_above"));
  auto* add_below = require_hotkey_action(window, QStringLiteral("select.add_layer_below"));
  auto* add_above = require_hotkey_action(window, QStringLiteral("select.add_layer_above"));
  CHECK(below == require_action(window, "selectLayerBelowAction"));
  CHECK(above == require_action(window, "selectLayerAboveAction"));
  CHECK(below->shortcut() == QKeySequence(Qt::ALT | Qt::Key_BracketLeft));
  CHECK(above->shortcut() == QKeySequence(Qt::ALT | Qt::Key_BracketRight));
  CHECK(add_below->shortcut() == QKeySequence(Qt::ALT | Qt::SHIFT | Qt::Key_BracketLeft));
  CHECK(add_above->shortcut() == QKeySequence(Qt::ALT | Qt::SHIFT | Qt::Key_BracketRight));
  // Layer Below and Layer Above follow Deselect Layers in the Select menu.
  auto* select_menu = menu_holding(window, below);
  CHECK(select_menu != nullptr);
  if (select_menu != nullptr) {
    const auto actions = select_menu->actions();
    const auto deselect = actions.indexOf(require_action(window, "selectDeselectLayersAction"));
    CHECK(deselect >= 0 && actions.indexOf(below) == deselect + 1 && actions.indexOf(above) == deselect + 2);
    CHECK(!actions.contains(add_below) && !actions.contains(add_above));
  }
  CHECK(below->menuRole() == QAction::NoRole && above->menuRole() == QAction::NoRole);

  CHECK((row_ids(*list) == std::vector{ids.top, ids.folder, ids.inner, ids.middle, ids.background}));
  const auto active = [&window] { return std::as_const(Access::document(window)).active_layer_id(); };
  const auto depth = Access::active_session_undo_depth(window);
  for (const auto expected : {ids.folder, ids.inner, ids.middle, ids.background, ids.background}) {
    below->trigger();
    QApplication::processEvents();
    CHECK(active() == expected);
    CHECK(Access::selected_layer_ids(window) == std::vector{expected});
  }
  above->trigger();
  QApplication::processEvents();
  CHECK(active() == ids.middle);
  Access::select_layers(window, {ids.top}, ids.top);
  QApplication::processEvents();
  above->trigger();  // the top row stays
  QApplication::processEvents();
  CHECK(active() == ids.top);
  CHECK(Access::active_session_undo_depth(window) == depth);

  // Extend: from Inner, add the rows below one at a time.
  Access::select_layers(window, {ids.inner}, ids.inner);
  QApplication::processEvents();
  add_below->trigger();
  QApplication::processEvents();
  CHECK(active() == ids.middle);
  auto selected = Access::selected_layer_ids(window);
  std::sort(selected.begin(), selected.end());
  auto expected = std::vector{ids.inner, ids.middle};
  std::sort(expected.begin(), expected.end());
  CHECK(selected == expected);
  add_below->trigger();
  QApplication::processEvents();
  CHECK(active() == ids.background);
  CHECK(Access::selected_layer_ids(window).size() == 3U);
  add_below->trigger();  // nothing below the Background
  QApplication::processEvents();
  CHECK(Access::selected_layer_ids(window).size() == 3U);
  // Adding above grows past the selection's top row (Inner), not the active one.
  add_above->trigger();
  QApplication::processEvents();
  CHECK(active() == ids.folder);
  CHECK(Access::selected_layer_ids(window).size() == 4U);

  // A collapsed folder is one row: Folder steps straight to Middle.
  Access::select_layers(window, {ids.folder}, ids.folder);
  QApplication::processEvents();
  auto* folder_row = list->itemWidget(require_layer_item(*list, QStringLiteral("Folder")));
  auto* disclosure =
      folder_row != nullptr ? folder_row->findChild<QToolButton*>(QStringLiteral("layerFolderDisclosureButton")) : nullptr;
  CHECK(disclosure != nullptr);
  if (disclosure != nullptr) {
    const auto center = disclosure->rect().center();
    send_mouse(*disclosure, QEvent::MouseButtonPress, center, Qt::LeftButton, Qt::LeftButton);
    send_mouse(*disclosure, QEvent::MouseButtonRelease, center, Qt::LeftButton, Qt::NoButton);
    process_events_until([&] { return list->count() == 4; });
    CHECK((row_ids(*list) == std::vector{ids.top, ids.folder, ids.middle, ids.background}));
    below->trigger();
    QApplication::processEvents();
    CHECK(active() == ids.middle);
  }
}

// GitHub issue 546: Layer > Hide Layers hides the selected layers when any shows, else shows
// them; the text follows. Outside history, like the eye.
void ui_layer_hide_show_layers_toggles_selection() {
  patchy::ui::MainWindow window;
  const auto ids = add_stack(window, QStringLiteral("Hide Layers"));
  auto* toggle = require_hotkey_action(window, QStringLiteral("layer.toggle_visibility"));
  CHECK(toggle == require_action(window, "layerToggleVisibilityAction"));
#ifdef Q_OS_MACOS
  CHECK(toggle->shortcut().isEmpty());  // Cmd+, is Preferences
#else
  CHECK(toggle->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_Comma));
#endif
  CHECK(toggle->menuRole() == QAction::NoRole);
  const auto visible = [&window](patchy::LayerId id) {
    const auto* layer = std::as_const(Access::document(window)).find_layer(id);
    return layer != nullptr && layer->visible();
  };
  Access::select_layers(window, {ids.middle, ids.top}, ids.top);
  QApplication::processEvents();
  CHECK(toggle->text() == QStringLiteral("&Hide Layers"));
  const auto depth = Access::active_session_undo_depth(window);
  toggle->trigger();
  QApplication::processEvents();
  CHECK(!visible(ids.middle) && !visible(ids.top) && visible(ids.folder) && visible(ids.background));
  CHECK(toggle->text() == QStringLiteral("S&how Layers"));
  CHECK(Access::selected_layer_ids(window).size() == 2U);  // the selection survives the row rebuild
  CHECK(window.statusBar()->currentMessage() == QStringLiteral("Layers hidden"));
  toggle->trigger();
  QApplication::processEvents();
  CHECK(visible(ids.middle) && visible(ids.top));
  CHECK(toggle->text() == QStringLiteral("&Hide Layers"));
  CHECK(Access::active_session_undo_depth(window) == depth);
  // A mixed selection hides every layer.
  Access::select_layers(window, {ids.top}, ids.top);
  QApplication::processEvents();
  toggle->trigger();
  QApplication::processEvents();
  Access::select_layers(window, {ids.middle, ids.top}, ids.top);
  QApplication::processEvents();
  CHECK(toggle->text() == QStringLiteral("&Hide Layers"));
  toggle->trigger();
  QApplication::processEvents();
  CHECK(!visible(ids.middle) && !visible(ids.top));
  // A folder hides as one layer, its child keeping its own eye.
  Access::select_layers(window, {ids.folder}, ids.folder);
  QApplication::processEvents();
  toggle->trigger();
  QApplication::processEvents();
  CHECK(!visible(ids.folder) && visible(ids.inner));
}

void ui_layer_lock_choices_follow_the_selection() {
  using patchy::ui::LayerLockChoice;
  const std::array<patchy::LayerLockFlags, 2> flags{
      patchy::kLayerLockPosition | patchy::kLayerLockTransparentPixels, patchy::kLayerLockTransparentPixels};
  const auto choices = patchy::ui::layer_lock_choices_for(flags);
  CHECK(choices.transparent_pixels == LayerLockChoice::On);
  CHECK(choices.image_pixels == LayerLockChoice::Off);
  CHECK(choices.position == LayerLockChoice::Mixed);
  CHECK(choices.auto_nesting == LayerLockChoice::Off);
  patchy::ui::LayerLockChoices chosen = choices;
  chosen.transparent_pixels = LayerLockChoice::Off;
  chosen.auto_nesting = LayerLockChoice::On;
  CHECK(patchy::ui::apply_layer_lock_choices(flags[0], chosen) ==
        (patchy::kLayerLockPosition | patchy::kLayerLockArtboardAutonest));
  CHECK(patchy::ui::apply_layer_lock_choices(flags[1], chosen) == patchy::kLayerLockArtboardAutonest);
  CHECK(patchy::ui::layer_lock_choices_for({}) == patchy::ui::LayerLockChoices{});
}

// GitHub issue 544: Layer > Lock Layers... prefills from the selection (a lock only some layers
// have starts partly checked), applies as one undo step, and keeps a mixed lock left alone.
void ui_layer_lock_layers_dialog_applies_and_undoes() {
  patchy::ui::MainWindow window;
  const auto ids = add_stack(window, QStringLiteral("Lock Layers"));
  auto* action = require_hotkey_action(window, QStringLiteral("layer.lock_layers"));
  CHECK(action == require_action(window, "layerLockLayersAction"));
  CHECK(action->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_Slash));
  CHECK(action->menuRole() == QAction::NoRole);
  // "/" alone stays Lock Transparent Pixels.
  CHECK(require_hotkey_action(window, QStringLiteral("layer.lock_transparent_pixels"))->shortcut() ==
        QKeySequence(Qt::Key_Slash));
  const auto flags_of = [&window](patchy::LayerId id) {
    const auto* layer = std::as_const(Access::document(window)).find_layer(id);
    return layer != nullptr ? patchy::layer_lock_flags(*layer) : patchy::kLayerLockNone;
  };
  if (auto* middle = Access::document(window).find_layer(ids.middle); middle != nullptr) {
    patchy::set_layer_lock_flag(*middle, patchy::kLayerLockPosition, true);
  }
  Access::select_layers(window, {ids.middle, ids.top}, ids.top);
  QApplication::processEvents();
  const auto depth = Access::active_session_undo_depth(window);

  bool driven = false;
  when_dialog_visible(QStringLiteral("layerLocksDialog"), [&](QDialog& dialog) {
    auto* transparent = dialog.findChild<QCheckBox*>(QStringLiteral("layerLocksTransparentCheck"));
    auto* image = dialog.findChild<QCheckBox*>(QStringLiteral("layerLocksImageCheck"));
    auto* position = dialog.findChild<QCheckBox*>(QStringLiteral("layerLocksPositionCheck"));
    auto* all = dialog.findChild<QCheckBox*>(QStringLiteral("layerLocksAllCheck"));
    auto* nest = dialog.findChild<QCheckBox*>(QStringLiteral("layerLocksAutoNestCheck"));
    CHECK(transparent != nullptr && image != nullptr && position != nullptr && all != nullptr && nest != nullptr);
    if (transparent == nullptr || image == nullptr || position == nullptr || all == nullptr || nest == nullptr) {
      return;
    }
    CHECK(transparent->checkState() == Qt::Unchecked);
    CHECK(position->checkState() == Qt::PartiallyChecked);
    CHECK(all->checkState() == Qt::PartiallyChecked);
    for (const auto scheme : {patchy::ui::ColorSchemePreference::Dark, patchy::ui::ColorSchemePreference::Light}) {
      ColorSchemeRestorer restore(scheme);
      QApplication::processEvents();
      save_widget_artifact(scheme == patchy::ui::ColorSchemePreference::Dark ? "issue544_lock_layers_dialog_dark"
                                                                            : "issue544_lock_layers_dialog_light",
                           dialog);
    }
    transparent->click();
    nest->click();
    CHECK(transparent->checkState() == Qt::Checked && nest->checkState() == Qt::Checked);
    CHECK(position->checkState() == Qt::PartiallyChecked);
    // A click on a partly checked box turns it on, the next one off; it never goes back to mixed.
    position->click();
    CHECK(position->checkState() == Qt::Checked);
    position->click();
    CHECK(position->checkState() == Qt::Unchecked);
    // All follows the three; a click on it sets them.
    all->click();
    CHECK(transparent->checkState() == Qt::Checked && image->checkState() == Qt::Checked &&
          position->checkState() == Qt::Checked && all->checkState() == Qt::Checked);
    image->click();
    position->click();
    CHECK(all->checkState() == Qt::PartiallyChecked);
    driven = true;
    dialog.accept();
  });
  action->trigger();
  QApplication::processEvents();
  CHECK(driven);
  CHECK(flags_of(ids.middle) == (patchy::kLayerLockTransparentPixels | patchy::kLayerLockArtboardAutonest));
  CHECK(flags_of(ids.top) == (patchy::kLayerLockTransparentPixels | patchy::kLayerLockArtboardAutonest));
  CHECK(Access::active_session_undo_depth(window) == depth + 1);
  CHECK(Access::selected_layer_ids(window).size() == 2U);
  CHECK(window.statusBar()->currentMessage() == QStringLiteral("Layer locks changed"));
  Access::undo(window);
  QApplication::processEvents();
  CHECK(flags_of(ids.middle) == patchy::kLayerLockPosition);
  CHECK(flags_of(ids.top) == patchy::kLayerLockNone);

  // A mixed lock left alone keeps each layer's state; Cancel writes nothing.
  Access::select_layers(window, {ids.middle, ids.top}, ids.top);
  QApplication::processEvents();
  when_dialog_visible(QStringLiteral("layerLocksDialog"), [](QDialog& dialog) {
    if (auto* image = dialog.findChild<QCheckBox*>(QStringLiteral("layerLocksImageCheck")); image != nullptr) {
      image->click();
    }
    dialog.accept();
  });
  action->trigger();
  QApplication::processEvents();
  CHECK(flags_of(ids.middle) == (patchy::kLayerLockPosition | patchy::kLayerLockImagePixels));
  CHECK(flags_of(ids.top) == patchy::kLayerLockImagePixels);
  const auto after = Access::active_session_undo_depth(window);
  when_dialog_visible(QStringLiteral("layerLocksDialog"), [](QDialog& dialog) {
    if (auto* all = dialog.findChild<QCheckBox*>(QStringLiteral("layerLocksAllCheck")); all != nullptr) {
      all->click();
    }
    dialog.reject();
  });
  action->trigger();
  QApplication::processEvents();
  CHECK(Access::active_session_undo_depth(window) == after);
  CHECK(flags_of(ids.top) == patchy::kLayerLockImagePixels);
}

// The new Layer menu rows (Hide Layers, Lock Layers..., the Export submenu last) within the
// menu's 26-row bound, the export keys (#545), and the menu for review in both schemes.
void ui_layer_menu_hosts_export_lock_and_hide_entries() {
  patchy::ui::MainWindow window;
  (void)add_stack(window, QStringLiteral("Layer Menu"));
  auto* layer_menu = window.findChild<QMenu*>(QStringLiteral("layerMenu"));
  auto* export_menu = window.findChild<QMenu*>(QStringLiteral("layerExportMenu"));
  CHECK(layer_menu != nullptr && export_menu != nullptr);
  if (layer_menu == nullptr || export_menu == nullptr) {
    return;
  }
  const auto actions = layer_menu->actions();
  CHECK(actions.size() <= 26);
  CHECK(actions.back() == export_menu->menuAction());
  CHECK(export_menu->menuAction()->menuRole() == QAction::NoRole);
  auto* quick = require_hotkey_action(window, QStringLiteral("layer.quick_export_png"));
  auto* export_as = require_hotkey_action(window, QStringLiteral("layer.export_as"));
  CHECK((export_menu->actions() == QList<QAction*>{quick, export_as}));
  CHECK(quick->shortcut() == QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Apostrophe));
  CHECK(export_as->shortcut() == QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::ALT | Qt::Key_Apostrophe));
  // The keys survive a portable-text round trip (the hotkey file) and stay apart from View > Grid.
  for (auto* action : {quick, export_as}) {
    CHECK(QKeySequence::fromString(action->shortcut().toString(QKeySequence::PortableText),
                                   QKeySequence::PortableText) == action->shortcut());
  }
  CHECK(QKeySequence::fromString(QStringLiteral("Ctrl+Shift+'"), QKeySequence::PortableText) == quick->shortcut());
  CHECK(require_hotkey_action(window, QStringLiteral("view.grid"))->shortcut() ==
        QKeySequence(Qt::CTRL | Qt::Key_Apostrophe));
  const auto color = actions.indexOf(window.findChild<QMenu*>(QStringLiteral("layerColorMenu"))->menuAction());
  CHECK(color > 0);
  CHECK(actions.indexOf(require_action(window, "layerToggleVisibilityAction")) == color + 1);
  CHECK(actions.indexOf(require_action(window, "layerLockLayersAction")) == color + 2);
  for (const auto scheme : {patchy::ui::ColorSchemePreference::Dark, patchy::ui::ColorSchemePreference::Light}) {
    ColorSchemeRestorer restore(scheme);
    const bool dark = scheme == patchy::ui::ColorSchemePreference::Dark;
    layer_menu->popup(window.mapToGlobal(QPoint(120, 40)));
    QApplication::processEvents();
    save_widget_artifact(dark ? "issue545_layer_menu_dark" : "issue545_layer_menu_light", *layer_menu);
    export_menu->popup(window.mapToGlobal(QPoint(420, 300)));
    QApplication::processEvents();
    save_widget_artifact(dark ? "issue545_layer_export_menu_dark" : "issue545_layer_export_menu_light", *export_menu);
    export_menu->hide();
    layer_menu->hide();
    QApplication::processEvents();
  }
  if (auto* select_menu = menu_holding(window, require_action(window, "selectLayerBelowAction"));
      select_menu != nullptr) {
    ColorSchemeRestorer restore(patchy::ui::ColorSchemePreference::Dark);
    select_menu->popup(window.mapToGlobal(QPoint(120, 40)));
    QApplication::processEvents();
    save_widget_artifact("issue541_select_menu_dark", *select_menu);
    select_menu->hide();
    QApplication::processEvents();
  }
}

}  // namespace

std::vector<patchy::test::TestCase> layer_menu_commands_tests() {
  return {
      {"ui_layer_select_below_above_steps_rows", ui_layer_select_below_above_steps_rows},
      {"ui_layer_hide_show_layers_toggles_selection", ui_layer_hide_show_layers_toggles_selection},
      {"ui_layer_lock_choices_follow_the_selection", ui_layer_lock_choices_follow_the_selection},
      {"ui_layer_lock_layers_dialog_applies_and_undoes", ui_layer_lock_layers_dialog_applies_and_undoes},
      {"ui_layer_menu_hosts_export_lock_and_hide_entries", ui_layer_menu_hosts_export_lock_and_hide_entries},
  };
}
