// Layer selection stepping (#541), the Lock Layers dialog (#544), Layer > Export (#545),
// Hide / Show Layers (#546), Show All Layers (#550) and the eye's Alt-click hint (#549):
// commands and default keys philipzae asked for.
#include "core/layer.hpp"
#include "core/layer_metadata.hpp"
#include "core/layer_tree.hpp"
#include "ui/canvas_widget.hpp"
#include "ui/layer_lock_dialog.hpp"
#include "ui/main_window.hpp"
#include "test_harness.hpp"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QEventLoop>
#include <QFont>
#include <QFontMetrics>
#include <QLayout>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QPushButton>
#include <QStatusBar>
#include <QTest>
#include <QTimer>
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

// GitHub issue 541: Alt+[ / Alt+] step the selection one panel row down / up and wrap around at
// the ends (the follow-up); Alt+Shift adds the next row past the selection and stops at the
// ends. A collapsed group is one row.
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
  // Below from the Background wraps to the top row.
  for (const auto expected : {ids.folder, ids.inner, ids.middle, ids.background, ids.top}) {
    below->trigger();
    QApplication::processEvents();
    CHECK(active() == expected);
    CHECK(Access::selected_layer_ids(window) == std::vector{expected});
  }
  // Above from the top row wraps to the Background, then climbs.
  for (const auto expected : {ids.background, ids.middle}) {
    above->trigger();
    QApplication::processEvents();
    CHECK(active() == expected);
    CHECK(Access::selected_layer_ids(window) == std::vector{expected});
  }
  // Extend stops at the top row.
  Access::select_layers(window, {ids.top}, ids.top);
  QApplication::processEvents();
  add_above->trigger();
  QApplication::processEvents();
  CHECK(active() == ids.top);
  CHECK(Access::selected_layer_ids(window) == std::vector{ids.top});
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

// GitHub issue 550: Layer > Show All Layers (Ctrl+Alt+,) shows every layer at every depth, like
// the eye menu's Show All Layers: outside history, marking the document modified.
void ui_layer_show_all_layers_shows_every_depth() {
  patchy::ui::MainWindow window;
  const auto ids = add_stack(window, QStringLiteral("Show All Layers"));
  auto* show_all = require_hotkey_action(window, QStringLiteral("layer.show_all_layers"));
  CHECK(show_all == require_action(window, "layerShowAllLayersAction"));
  CHECK(show_all->shortcut() == QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_Comma));
  CHECK(show_all->menuRole() == QAction::NoRole);
  CHECK(show_all->text() == QStringLiteral("Sh&ow All Layers"));
  const auto visible = [&window](patchy::LayerId id) {
    const auto* layer = std::as_const(Access::document(window)).find_layer(id);
    return layer != nullptr && layer->visible();
  };
  for (const auto id : {ids.middle, ids.folder, ids.inner, ids.top}) {
    Access::document(window).find_layer(id)->set_visible(false);
  }
  Access::refresh_layer_ui(window);
  QApplication::processEvents();
  Access::mark_active_session_saved(window);
  const auto depth = Access::active_session_undo_depth(window);
  show_all->trigger();
  QApplication::processEvents();
  for (const auto id : {ids.background, ids.middle, ids.folder, ids.inner, ids.top}) {
    CHECK(visible(id));
  }
  CHECK(window.statusBar()->currentMessage() == QStringLiteral("Showed all layers"));
  CHECK(Access::active_session_undo_depth(window) == depth);
  CHECK(Access::active_session_is_modified(window));
}

// GitHub issue 549: the eye's tooltip names the Alt-click (Option-click) isolation.
void ui_layer_eye_tooltip_names_alt_click() {
  patchy::ui::MainWindow window;
  const auto ids = add_stack(window, QStringLiteral("Eye Tooltip"));
  auto* list = layer_list(window);
  if (list == nullptr) {
    return;
  }
  const auto eye_of = [&](const QString& name) -> QToolButton* {
    auto* row = list->itemWidget(require_layer_item(*list, name));
    return row != nullptr ? row->findChild<QToolButton*>(QStringLiteral("layerVisibilityCheck")) : nullptr;
  };
#ifdef Q_OS_MACOS
  const QString alt = QStringLiteral("Option");
#else
  const QString alt = QStringLiteral("Alt");
#endif
  auto* eye = eye_of(QStringLiteral("Middle"));
  CHECK(eye != nullptr);
  if (eye != nullptr) {
    CHECK(eye->toolTip() ==
          QStringLiteral("Layer visible. Click to hide. %1-click to show only this layer.").arg(alt));
    // Issue shot: the Layers list over the tip Qt shows for the eye.
    ColorSchemeRestorer restore(patchy::ui::ColorSchemePreference::Dark);
    QApplication::processEvents();
    if (auto* shown_eye = eye_of(QStringLiteral("Middle")); shown_eye != nullptr) {
      const auto tip = render_widget_tooltip(*shown_eye);
      CHECK(!tip.isNull());
      if (!tip.isNull()) {
        save_stacked_artifact("issue549_eye_tooltip_dark", {list->grab().toImage(), tip});
      }
    }
  }
  Access::document(window).find_layer(ids.middle)->set_visible(false);
  Access::refresh_layer_ui(window);
  QApplication::processEvents();
  eye = eye_of(QStringLiteral("Middle"));
  CHECK(eye != nullptr);
  if (eye != nullptr) {
    CHECK(eye->toolTip() ==
          QStringLiteral("Layer hidden. Click to show. %1-click to show only this layer.").arg(alt));
  }
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
    // Room for the whole title beside a system title bar's icon and buttons (#544 follow-up:
    // KDE showed "Lock...yers" at the layout's own width).
    QFont title_font = dialog.font();
    title_font.setBold(true);
    const int title_room = QFontMetrics(title_font).horizontalAdvance(dialog.windowTitle()) + 200;
    CHECK(dialog.minimumWidth() >= title_room);
    CHECK(dialog.width() >= title_room);
    CHECK(dialog.width() > dialog.layout()->sizeHint().width());
    // OK as wide as Cancel (#544 follow-up 2).
    auto* button_box = dialog.findChild<QDialogButtonBox*>();
    CHECK(button_box != nullptr);
    if (button_box != nullptr) {
      auto* ok = button_box->button(QDialogButtonBox::Ok);
      auto* cancel = button_box->button(QDialogButtonBox::Cancel);
      CHECK(ok != nullptr && cancel != nullptr);
      if (ok != nullptr && cancel != nullptr) {
        CHECK(ok->width() == cancel->width());
        CHECK(ok->width() >= std::max(ok->sizeHint().width(), cancel->sizeHint().width()));
      }
    }
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

// The Layer menu's Hide & Lock and Export entries (#544 to #550, #627) within the menu's 22-row
// bound, the export keys (#545), and the menu for review in both schemes.
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
  CHECK(std::count_if(actions.begin(), actions.end(), [](const QAction* action) { return !action->isSeparator(); }) <=
        22);
  // Export sits between Duplicate Layer (and its Rasterize submenu) and Delete Layer (#627).
  const auto export_row = actions.indexOf(export_menu->menuAction());
  CHECK(export_row > 1 && actions.at(export_row - 2) == require_action(window, "layerDuplicateAction"));
  auto* rasterize_menu = window.findChild<QMenu*>(QStringLiteral("layerRasterizeMenu"));
  CHECK(rasterize_menu != nullptr && export_row > 0 && actions.at(export_row - 1) == rasterize_menu->menuAction());
  CHECK(export_row + 1 < actions.size() && actions.at(export_row + 1) == require_action(window, "layerDeleteAction"));
  CHECK(export_menu->menuAction()->menuRole() == QAction::NoRole);
  auto* quick = require_hotkey_action(window, QStringLiteral("layer.quick_export_png"));
  auto* export_as = require_hotkey_action(window, QStringLiteral("layer.export_as"));
  auto* to_document = require_hotkey_action(window, QStringLiteral("layer.duplicate_to_document"));
  const auto export_actions = export_menu->actions();
  CHECK(export_actions.size() == 4);
  if (export_actions.size() == 4) {
    // Duplicate Layer to Document... after a line, as in the layer context menu's Export.
    CHECK(export_actions.at(0) == quick && export_actions.at(1) == export_as);
    CHECK(export_actions.at(2)->isSeparator() && export_actions.at(3) == to_document);
  }
  CHECK(to_document->menuRole() == QAction::NoRole);
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
  // Hide Layers, Show All Layers and Lock Layers... open Layer > Hide & Lock (#627).
  auto* hide_lock = window.findChild<QMenu*>(QStringLiteral("layerHideLockMenu"));
  CHECK(hide_lock != nullptr);
  if (hide_lock != nullptr) {
    const auto rows = hide_lock->actions();
    CHECK(rows.size() > 3);
    if (rows.size() > 3) {
      CHECK(rows.at(0) == require_action(window, "layerToggleVisibilityAction"));
      CHECK(rows.at(1) == require_action(window, "layerShowAllLayersAction"));
      CHECK(rows.at(2)->isSeparator());
      CHECK(rows.at(3) == require_action(window, "layerLockLayersAction"));
    }
  }
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

// The Layer menu's rows by object name ("-" for a separator, a submenu by its menu's name).
QStringList layer_menu_rows(QMenu& menu) {
  QStringList rows;
  for (auto* action : menu.actions()) {
    rows.append(action->isSeparator()    ? QStringLiteral("-")
                : action->menu() != nullptr ? action->menu()->objectName()
                                            : action->objectName());
  }
  return rows;
}

// The reorganized Layer menu (Seth, October 31, 2026; GitHub issue 627): six groups, with the
// fill and merge commands in Fill and Merge submenus that keep their shortcuts, which still
// fire from there.
void ui_layer_menu_groups_fill_and_merge_submenus() {
  patchy::ui::MainWindow window;
  const auto ids = add_stack(window, QStringLiteral("Layer Groups"));
  auto* layer_menu = window.findChild<QMenu*>(QStringLiteral("layerMenu"));
  auto* fill_menu = window.findChild<QMenu*>(QStringLiteral("layerFillMenu"));
  auto* merge_menu = window.findChild<QMenu*>(QStringLiteral("layerMergeMenu"));
  CHECK(layer_menu != nullptr && fill_menu != nullptr && merge_menu != nullptr);
  if (layer_menu == nullptr || fill_menu == nullptr || merge_menu == nullptr) {
    return;
  }
  CHECK((layer_menu_rows(*layer_menu) ==
         QStringList{"layerNewMenu", "layerNewFillMenu", "layerNewAdjustmentMenu", "-", "layerDuplicateAction",
                     "layerRasterizeMenu", "layerExportMenu", "layerDeleteAction", "-", "layerRenameAction",
                     "layerColorMenu", "layerHideLockMenu", "-", "layerStyleMenu", "layerFillAdjustmentMenu", "-",
                     "layerMaskMenu", "layerVectorMaskMenu", "layerClippingMaskAction", "-", "layerSmartObjectsMenu",
                     "layerShapeMenu", "-", "layerFillMenu", "layerArrangeMenu", "layerRotateFlipMenu",
                     "layerMergeMenu"}));
  const auto top_rows = layer_menu->actions();
  CHECK(std::count_if(top_rows.begin(), top_rows.end(), [](const QAction* action) { return !action->isSeparator(); }) ==
        21);
  // Accelerators stay unique on each level.
  for (auto* menu : {layer_menu, fill_menu, merge_menu}) {
    QStringList keys;
    for (auto* action : menu->actions()) {
      const auto text = action->text();
      const auto amp = text.indexOf(QLatin1Char('&'));
      if (amp >= 0 && amp + 1 < text.size()) {
        const auto key = text.mid(amp + 1, 1).toLower();
        CHECK(!keys.contains(key));
        keys.append(key);
      }
    }
  }
  struct Entry {
    const char* name;
    QKeySequence keys;
  };
  const auto check_entries = [&window](QMenu& menu, std::initializer_list<Entry> entries) {
    CHECK(menu.menuAction()->menuRole() == QAction::NoRole);
    QList<QAction*> expected;
    for (const auto& entry : entries) {
      auto* action = require_action(window, entry.name);
      expected.append(action);
      CHECK(action->shortcut() == entry.keys);
      CHECK(action->menuRole() == QAction::NoRole);
    }
    CHECK(menu.actions() == expected);
  };
  check_entries(*fill_menu, {{"layerFillForegroundAction", QKeySequence(Qt::ALT | Qt::Key_Backspace)},
                             {"layerFillBackgroundAction", QKeySequence(Qt::CTRL | Qt::Key_Backspace)},
#ifdef Q_OS_MACOS
                             // The Mac delete key sends Backspace; forward delete is the second key.
                             {"layerClearAction", QKeySequence(Qt::Key_Backspace)}});
#else
                             {"layerClearAction", QKeySequence(Qt::Key_Delete)}});
#endif
  check_entries(*merge_menu, {{"layerMergeDownAction", QKeySequence(Qt::CTRL | Qt::Key_E)},
                              {"layerMergeVisibleLayersAction", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_E)},
                              {"layerMergeVisibleAction", QKeySequence(Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_E)}});
  CHECK(require_action(window, "layerDeleteAction")->text() == QStringLiteral("Delete La&yer"));

  // Opening the Merge submenu refreshes Merge Visible's state on its own.
  auto* merge_visible = require_action(window, "layerMergeVisibleLayersAction");
  merge_visible->setEnabled(false);
  merge_menu->popup(window.mapToGlobal(QPoint(200, 60)));
  QApplication::processEvents();
  CHECK(merge_visible->isEnabled());
  merge_menu->hide();
  QApplication::processEvents();

  // The keys still reach the commands inside the submenus.
  auto* canvas = require_canvas(window);
  const auto top_level_count = [&window] { return std::as_const(Access::document(window)).layers().size(); };
  Access::select_layers(window, {ids.middle}, ids.middle);
  QApplication::processEvents();
  canvas->setFocus();
  const auto depth = Access::active_session_undo_depth(window);
  QTest::keyClick(canvas, Qt::Key_Backspace, Qt::ControlModifier);
  QApplication::processEvents();
  CHECK(Access::active_session_undo_depth(window) == depth + 1);
  const auto layers_before = top_level_count();
  QTest::keyClick(canvas, Qt::Key_E, Qt::ControlModifier);
  QApplication::processEvents();
  CHECK(top_level_count() == layers_before - 1);

  for (const auto scheme : {patchy::ui::ColorSchemePreference::Dark, patchy::ui::ColorSchemePreference::Light}) {
    ColorSchemeRestorer restore(scheme);
    const std::string suffix = scheme == patchy::ui::ColorSchemePreference::Dark ? "_dark" : "_light";
    layer_menu->popup(window.mapToGlobal(QPoint(120, 40)));
    QApplication::processEvents();
    save_widget_artifact("layer_menu_reorg_layer_menu" + suffix, *layer_menu);
    for (auto* submenu : {fill_menu, merge_menu}) {
      submenu->popup(window.mapToGlobal(QPoint(420, 300)));
      QApplication::processEvents();
      save_widget_artifact("layer_menu_reorg_" + submenu->objectName().toStdString() + suffix, *submenu);
      submenu->hide();
    }
    layer_menu->hide();
    QApplication::processEvents();
  }
}

// GitHub issue 627: Layer > Hide & Lock, Layer > Layer Style and Layer > Fill and Adjustment
// Layer. Their entries share the layer context menu's actions or handlers, so checks follow the
// selection, and the moved commands keep their command ids (Edit Layer Styles... stays on the
// window for its key and Search Commands).
void ui_issue_627_layer_menu_submenus() {
  patchy::ui::MainWindow window;
  const auto ids = add_stack(window, QStringLiteral("Layer Menu Submenus"));
  auto* hide_lock = window.findChild<QMenu*>(QStringLiteral("layerHideLockMenu"));
  auto* style_menu = window.findChild<QMenu*>(QStringLiteral("layerStyleMenu"));
  auto* fill_adjustment = window.findChild<QMenu*>(QStringLiteral("layerFillAdjustmentMenu"));
  CHECK(hide_lock != nullptr && style_menu != nullptr && fill_adjustment != nullptr);
  if (hide_lock == nullptr || style_menu == nullptr || fill_adjustment == nullptr) {
    return;
  }
  CHECK(hide_lock->title() == QStringLiteral("&Hide && Lock"));
  CHECK(style_menu->title() == QStringLiteral("Layer &Style"));
  CHECK(fill_adjustment->title() == QStringLiteral("Fi&ll and Adjustment Layer"));
  const auto rows = [](QMenu& menu) {
    QStringList names;
    for (auto* action : menu.actions()) {
      names.append(action->isSeparator() ? QStringLiteral("-") : action->objectName());
      // macOS merges menu items by role and title ("Options" means Preferences there).
      CHECK(action->isSeparator() || action->menuRole() == QAction::NoRole);
    }
    CHECK(menu.menuAction()->menuRole() == QAction::NoRole);
    return names;
  };
  CHECK((rows(*hide_lock) ==
         QStringList{"layerToggleVisibilityAction", "layerShowAllLayersAction", "-", "layerLockLayersAction", "-",
                     "layerLockTransparentPixelsAction", "layerMenuLockImagePixelsAction",
                     "layerMenuLockPositionAction", "-", "layerMenuLockAllAction", "-",
                     "layerPreventAutoNestingAction"}));
  CHECK((rows(*style_menu) ==
         QStringList{"layerMenuStyleBlendingOptionsAction", "layerMenuStyleBevelEmbossAction",
                     "layerMenuStyleStrokeAction", "layerMenuStyleInnerShadowAction", "layerMenuStyleInnerGlowAction",
                     "layerMenuStyleSatinAction", "layerMenuStyleColorOverlayAction",
                     "layerMenuStyleGradientOverlayAction", "layerMenuStylePatternOverlayAction",
                     "layerMenuStyleOuterGlowAction", "layerMenuStyleDropShadowAction", "-", "layerCopyStyleAction",
                     "layerPasteStyleAction", "layerDeleteStyleAction", "-", "layerMenuShowLayerStylesAction"}));
  CHECK((rows(*fill_adjustment) == QStringList{"layerFillLayerSettingsAction", "layerEditAdjustmentAction"}));
  CHECK(require_action(window, "layerMenuStyleBlendingOptionsAction")->text() == QStringLiteral("Blending Options..."));
  CHECK(require_action(window, "layerMenuStyleBevelEmbossAction")->text() == QStringLiteral("Bevel && Emboss..."));
  CHECK(require_action(window, "layerFillLayerSettingsAction")->text() == QStringLiteral("&Fill Layer Settings..."));

  // Command ids are persisted: the renamed and moved commands keep theirs.
  auto* adjustment = require_hotkey_action(window, QStringLiteral("layer.edit_adjustment"));
  CHECK(adjustment == require_action(window, "layerEditAdjustmentAction"));
  CHECK(adjustment->text() == QStringLiteral("&Adjustment Layer Settings..."));
  CHECK(require_hotkey_action(window, QStringLiteral("layer.fill_layer_settings")) ==
        require_action(window, "layerFillLayerSettingsAction"));
  auto* edit_styles = require_hotkey_action(window, QStringLiteral("layer.styles"));
  CHECK(edit_styles == require_action(window, "layerBlendingOptionsAction"));
  CHECK(edit_styles->text() == QStringLiteral("Edit Layer &Styles..."));
  CHECK(window.actions().contains(edit_styles));
  for (const auto* id : {"layer.toggle_visibility", "layer.show_all_layers", "layer.lock_layers",
                         "layer.lock_transparent_pixels", "layer.prevent_auto_nesting"}) {
    auto* action = require_hotkey_action(window, QString::fromLatin1(id));
    CHECK(hide_lock->actions().contains(action));
  }

  // The checks follow the selection when the submenu opens, as the context menu's Lock submenu.
  const auto flags_of = [&window](patchy::LayerId id) {
    const auto* layer = std::as_const(Access::document(window)).find_layer(id);
    return layer != nullptr ? patchy::layer_lock_flags(*layer) : patchy::kLayerLockNone;
  };
  if (auto* top = Access::document(window).find_layer(ids.top); top != nullptr) {
    patchy::set_layer_lock_flag(*top, patchy::kLayerLockPosition, true);
  }
  Access::select_layers(window, {ids.top}, ids.top);
  QApplication::processEvents();
  auto* lock_position = require_action(window, "layerMenuLockPositionAction");
  auto* lock_image = require_action(window, "layerMenuLockImagePixelsAction");
  auto* lock_all = require_action(window, "layerMenuLockAllAction");
  hide_lock->popup(window.mapToGlobal(QPoint(200, 60)));
  QApplication::processEvents();
  CHECK(lock_position->isChecked());
  CHECK(!lock_image->isChecked() && !lock_all->isChecked());
  hide_lock->hide();
  QApplication::processEvents();
  const auto depth = Access::active_session_undo_depth(window);
  lock_image->trigger();
  QApplication::processEvents();
  CHECK(flags_of(ids.top) == (patchy::kLayerLockPosition | patchy::kLayerLockImagePixels));
  CHECK(Access::active_session_undo_depth(window) == depth + 1);
  lock_position->trigger();
  QApplication::processEvents();
  CHECK(flags_of(ids.top) == patchy::kLayerLockImagePixels);
  lock_image->trigger();
  QApplication::processEvents();
  CHECK(flags_of(ids.top) == patchy::kLayerLockNone);

  // Layer Style: no style yet, so the clipboard's Delete and the effects switch are off; the
  // pages open the one dialog.
  style_menu->popup(window.mapToGlobal(QPoint(200, 60)));
  QApplication::processEvents();
  auto* show_styles = require_action(window, "layerMenuShowLayerStylesAction");
  CHECK(show_styles->isCheckable());
  CHECK(!show_styles->isEnabled());
  CHECK(!require_action(window, "layerDeleteStyleAction")->isEnabled());
  CHECK(require_action(window, "layerCopyStyleAction")->isEnabled());
  CHECK(require_action(window, "layerMenuStyleDropShadowAction")->isEnabled());
  style_menu->hide();
  QApplication::processEvents();
  bool saw_dialog = false;
  when_dialog_visible(QStringLiteral("patchyLayerStyleDialog"), [&saw_dialog](QDialog& dialog) {
    saw_dialog = true;
    dialog.accept();
  });
  require_action(window, "layerMenuStyleDropShadowAction")->trigger();
  QApplication::processEvents();
  CHECK(saw_dialog);
  const auto style_of_top = [&window, &ids]() -> patchy::LayerStyle {
    const auto* layer = std::as_const(Access::document(window)).find_layer(ids.top);
    return layer != nullptr ? layer->layer_style() : patchy::LayerStyle{};
  };
  CHECK(style_of_top().drop_shadows.size() == 1U);
  CHECK(style_of_top().effects_visible);
  style_menu->popup(window.mapToGlobal(QPoint(200, 60)));
  QApplication::processEvents();
  CHECK(show_styles->isEnabled() && show_styles->isChecked());
  CHECK(require_action(window, "layerDeleteStyleAction")->isEnabled());
  style_menu->hide();
  QApplication::processEvents();
  show_styles->trigger();
  QApplication::processEvents();
  CHECK(!style_of_top().effects_visible);
  CHECK(style_of_top().drop_shadows.size() == 1U);
  style_menu->popup(window.mapToGlobal(QPoint(200, 60)));
  QApplication::processEvents();
  CHECK(show_styles->isEnabled() && !show_styles->isChecked());
  style_menu->hide();
  QApplication::processEvents();

  // Fill Layer Settings... refuses a pixel layer and leaves it as it was.
  const auto fill_depth = Access::active_session_undo_depth(window);
  require_action(window, "layerFillLayerSettingsAction")->trigger();
  QApplication::processEvents();
  CHECK(Access::active_session_undo_depth(window) == fill_depth);
  CHECK(window.statusBar()->currentMessage() == QStringLiteral("Select a fill layer to edit its settings"));

  // Review captures (issue desk): the Layer menu and its new submenus.
  auto* layer_menu = window.findChild<QMenu*>(QStringLiteral("layerMenu"));
  CHECK(layer_menu != nullptr);
  if (layer_menu == nullptr) {
    return;
  }
  ColorSchemeRestorer restore(patchy::ui::ColorSchemePreference::Dark);
  layer_menu->popup(window.mapToGlobal(QPoint(120, 40)));
  QApplication::processEvents();
  save_widget_artifact("issue_627_layer_menu", *layer_menu);
  for (auto* submenu : {hide_lock, style_menu, fill_adjustment}) {
    submenu->popup(window.mapToGlobal(QPoint(420, 120)));
    QApplication::processEvents();
    save_widget_artifact("issue_627_" + submenu->objectName().toStdString(), *submenu);
    submenu->hide();
    QApplication::processEvents();
  }
  layer_menu->hide();
  QApplication::processEvents();
}

// The Layers panel context menu's Rasterize rows for `id`, by object name ("-" for a separator).
QStringList context_rasterize_rows(patchy::ui::MainWindow& window, patchy::LayerId id) {
  auto* list = layer_list(window);
  if (list == nullptr) {
    return {};
  }
  const auto ids = row_ids(*list);
  const auto row = static_cast<int>(std::find(ids.begin(), ids.end(), id) - ids.begin());
  CHECK(row < list->count());
  if (row >= list->count()) {
    return {};
  }
  QStringList rows;
  bool saw_menu = false;
  int poll_attempts = 0;
  QTimer poller;
  QObject::connect(&poller, &QTimer::timeout, [&] {
    if (++poll_attempts > 500) {
      poller.stop();
      return;
    }
    for (auto* widget : QApplication::topLevelWidgets()) {
      auto* menu = qobject_cast<QMenu*>(widget);
      if (menu != nullptr && menu->objectName() == QStringLiteral("layerContextMenu") && menu->isVisible()) {
        saw_menu = true;
        if (auto* rasterize = menu->findChild<QMenu*>(QStringLiteral("layerContextRasterizeMenu"));
            rasterize != nullptr) {
          for (auto* action : rasterize->actions()) {
            rows << (action->isSeparator() ? QStringLiteral("-") : action->objectName());
          }
        }
        menu->close();
        poller.stop();
        return;
      }
    }
  });
  poller.start(10);
  const auto position = list->visualItemRect(list->item(row)).center();
  QMetaObject::invokeMethod(
      &window, [&window, position] { Access::show_layer_context_menu(window, position); }, Qt::QueuedConnection);
  QApplication::processEvents();
  for (int i = 0; i < 200 && !saw_menu && poll_attempts <= 500; ++i) {
    QApplication::processEvents(QEventLoop::AllEvents, 20);
  }
  poller.stop();
  CHECK(saw_menu);
  return rows;
}

// GitHub issue 627 follow-up: Layer > Rasterize sits under Duplicate Layer and holds the Layers
// panel context menu's Rasterize rows (one builder, the same actions), the text rows only while
// a text layer is active.
void ui_issue_627_layer_menu_rasterize_submenu() {
  patchy::Document document(140, 96, patchy::PixelFormat::rgba8());
  const auto photo_id = document.add_layer(square_layer(document, "Photo", QColor(220, 60, 50))).id();
  patchy::Layer text_layer(document.allocate_layer_id(), "Text: Raster", patchy::LayerKind::Text);
  text_layer.set_bounds(patchy::Rect{24, 24, 96, 36});
  text_layer.metadata()[patchy::kLayerMetadataText] = "Raster";
  text_layer.metadata()[patchy::kLayerMetadataTextSize] = "28";
  text_layer.metadata()[patchy::kLayerMetadataTextColor] = "#000000";
  const auto text_id = document.add_layer(std::move(text_layer)).id();
  document.set_active_layer(text_id);
  patchy::ui::MainWindow window;
  window.add_document_session(std::move(document), QStringLiteral("Rasterize Menu"));
  show_window(window);
  QApplication::processEvents();

  auto* layer_menu = window.findChild<QMenu*>(QStringLiteral("layerMenu"));
  auto* rasterize_menu = window.findChild<QMenu*>(QStringLiteral("layerRasterizeMenu"));
  CHECK(layer_menu != nullptr && rasterize_menu != nullptr);
  if (layer_menu == nullptr || rasterize_menu == nullptr) {
    return;
  }
  CHECK(rasterize_menu->title() == QStringLiteral("Rasterize"));
  CHECK(rasterize_menu->menuAction()->menuRole() == QAction::NoRole);
  const auto duplicate_row = layer_menu->actions().indexOf(require_action(window, "layerDuplicateAction"));
  CHECK(duplicate_row >= 0 && layer_menu->actions().value(duplicate_row + 1) == rasterize_menu->menuAction());
  const auto layer_menu_rows = [&window, rasterize_menu] {
    rasterize_menu->popup(window.mapToGlobal(QPoint(200, 60)));
    QApplication::processEvents();
    QStringList rows;
    for (auto* action : rasterize_menu->actions()) {
      rows << (action->isSeparator() ? QStringLiteral("-") : action->objectName());
      // macOS merges menu items by role and title.
      CHECK(action->isSeparator() || action->menuRole() == QAction::NoRole);
    }
    rasterize_menu->hide();
    QApplication::processEvents();
    return rows;
  };
  const QStringList pixel_rows{"layerRasterizeAction", "layerRasterizeLayerStyleAction", "-",
                               "layerTraceImageAction"};
  const QStringList text_rows = pixel_rows + QStringList{"-", "typeConvertToShapeAction", "typeCreateWorkPathAction"};
  auto* rasterize = require_action(window, "layerRasterizeAction");

  Access::select_layers(window, {text_id}, text_id);
  QApplication::processEvents();
  CHECK(layer_menu_rows() == text_rows);
  CHECK(rasterize->isEnabled());
  CHECK(context_rasterize_rows(window, text_id) == text_rows);

  Access::select_layers(window, {photo_id}, photo_id);
  QApplication::processEvents();
  CHECK(layer_menu_rows() == pixel_rows);
  CHECK(!rasterize->isEnabled());
  CHECK(context_rasterize_rows(window, photo_id) == pixel_rows);

  // Review captures (issue desk): the Layer menu and its Rasterize submenu on a text layer.
  Access::select_layers(window, {text_id}, text_id);
  QApplication::processEvents();
  {
    ColorSchemeRestorer restore(patchy::ui::ColorSchemePreference::Dark);
    layer_menu->popup(window.mapToGlobal(QPoint(120, 40)));
    QApplication::processEvents();
    save_widget_artifact("issue_627_fu_layer_menu", *layer_menu);
    rasterize_menu->popup(window.mapToGlobal(QPoint(420, 120)));
    QApplication::processEvents();
    save_widget_artifact("issue_627_fu_layer_rasterize_menu", *rasterize_menu);
    rasterize_menu->hide();
    layer_menu->hide();
    QApplication::processEvents();
  }

  // Rasterize from the Layer menu turns the text layer into pixels, one undo step.
  const auto depth = Access::active_session_undo_depth(window);
  rasterize->trigger();
  QApplication::processEvents();
  const auto* rasterized = std::as_const(Access::document(window)).find_layer(text_id);
  CHECK(rasterized != nullptr && rasterized->kind() == patchy::LayerKind::Pixel);
  CHECK(Access::active_session_undo_depth(window) == depth + 1);
}

// The Layers panel context menu's top-level rows for `id` (already selected), by object name,
// a submenu by its menu's object name, an unnamed row by its text, "-" for a separator. Saves a
// review shot named `artifact` when it is not empty.
QStringList context_menu_rows(patchy::ui::MainWindow& window, patchy::LayerId id, const std::string& artifact) {
  auto* list = layer_list(window);
  if (list == nullptr) {
    return {};
  }
  const auto ids = row_ids(*list);
  const auto row = static_cast<int>(std::find(ids.begin(), ids.end(), id) - ids.begin());
  CHECK(row < list->count());
  if (row >= list->count()) {
    return {};
  }
  QStringList rows;
  bool saw_menu = false;
  int poll_attempts = 0;
  QTimer poller;
  QObject::connect(&poller, &QTimer::timeout, [&] {
    if (++poll_attempts > 500) {
      poller.stop();
      return;
    }
    for (auto* widget : QApplication::topLevelWidgets()) {
      auto* menu = qobject_cast<QMenu*>(widget);
      if (menu != nullptr && menu->objectName() == QStringLiteral("layerContextMenu") && menu->isVisible()) {
        saw_menu = true;
        for (auto* action : menu->actions()) {
          if (action->isSeparator()) {
            rows << QStringLiteral("-");
          } else if (action->menu() != nullptr) {
            rows << action->menu()->objectName();
          } else {
            rows << (action->objectName().isEmpty() ? action->text() : action->objectName());
          }
        }
        if (!artifact.empty()) {
          save_widget_artifact(artifact, *menu);
        }
        menu->close();
        poller.stop();
        return;
      }
    }
  });
  poller.start(10);
  const auto position = list->visualItemRect(list->item(row)).center();
  QMetaObject::invokeMethod(
      &window, [&window, position] { Access::show_layer_context_menu(window, position); }, Qt::QueuedConnection);
  QApplication::processEvents();
  for (int i = 0; i < 200 && !saw_menu && poll_attempts <= 500; ++i) {
    QApplication::processEvents(QEventLoop::AllEvents, 20);
  }
  poller.stop();
  CHECK(saw_menu);
  return rows;
}

// The rows that show the link badge, by layer id.
std::vector<patchy::LayerId> linked_badge_rows(QListWidget& list) {
  std::vector<patchy::LayerId> ids;
  for (int row = 0; row < list.count(); ++row) {
    auto* widget = list.itemWidget(list.item(row));
    if (widget != nullptr && widget->findChild<QToolButton*>(QStringLiteral("layerLinkBadgeButton")) != nullptr) {
      ids.push_back(static_cast<patchy::LayerId>(list.item(row)->data(Qt::UserRole).toULongLong()));
    }
  }
  std::sort(ids.begin(), ids.end());
  return ids;
}

// GitHub issue 656: Layer > Arrange > Link Layers (one entry that reads Unlink Layers when the
// selection is one link group, like Hide/Show Layers) and Select Linked Layers act on the
// selection as one undo step each, follow it with their enabled states, badge the linked rows;
// the layer.unlink key stays outside the menus and always unlinks, and the footer's optional
// link button follows the same rule. The row menu sets Color and the link commands apart.
void ui_layer_link_commands_link_unlink_undo_and_badges() {
  patchy::ui::MainWindow window;
  const auto ids = add_stack(window, QStringLiteral("Link Layers"));
  auto* list = layer_list(window);
  auto* link = require_action(window, "layerLinkAction");
  auto* unlink = require_action(window, "layerUnlinkAction");
  auto* select_linked = require_action(window, "layerSelectLinkedAction");
  auto* arrange = window.findChild<QMenu*>(QStringLiteral("layerArrangeMenu"));
  CHECK(list != nullptr && arrange != nullptr);
  if (list == nullptr || arrange == nullptr) {
    return;
  }
  CHECK(arrange->actions().contains(link) && !arrange->actions().contains(unlink) &&
        arrange->actions().contains(select_linked));
  // The key-only Unlink Layers sits on the window and in no menu.
  CHECK(window.actions().contains(unlink));
  const auto unlink_hosts = unlink->associatedObjects();
  CHECK(std::none_of(unlink_hosts.begin(), unlink_hosts.end(),
                     [](QObject* host) { return qobject_cast<QMenu*>(host) != nullptr; }));
  for (auto* action : {link, unlink, select_linked}) {
    CHECK(action->menuRole() == QAction::NoRole);
  }
  CHECK(require_hotkey_action(window, QStringLiteral("layer.link")) == link);
  CHECK(require_hotkey_action(window, QStringLiteral("layer.unlink")) == unlink);
  CHECK(require_hotkey_action(window, QStringLiteral("layer.select_linked")) == select_linked);
  const auto& layers = [&window]() -> const std::vector<patchy::Layer>& {
    return std::as_const(Access::document(window)).layers();
  };
  const auto group_of = [&](patchy::LayerId id) { return patchy::effective_link_group(layers(), id); };

  // One layer: nothing to link.
  Access::select_layers(window, {ids.top}, ids.top);
  QApplication::processEvents();
  CHECK(!link->isEnabled() && !unlink->isEnabled() && !select_linked->isEnabled());

  CHECK(link->text() == QStringLiteral("&Link Layers"));

  Access::select_layers(window, {ids.top, ids.middle}, ids.top);
  QApplication::processEvents();
  CHECK(link->isEnabled() && !unlink->isEnabled());
  CHECK(link->text() == QStringLiteral("&Link Layers"));
  // The row menu for two unlinked layers: Delete, then Color apart, then the one link entry.
  const auto unlinked_rows = context_menu_rows(window, ids.top, "");
  const auto color_row = unlinked_rows.indexOf(QStringLiteral("layerContextColorMenu"));
  CHECK(color_row > 0 && unlinked_rows.value(color_row - 1) == QStringLiteral("-"));
  CHECK(unlinked_rows.count(QStringLiteral("layerLinkAction")) == 1);
  CHECK(!unlinked_rows.contains(QStringLiteral("layerUnlinkAction")) &&
        !unlinked_rows.contains(QStringLiteral("layerSelectLinkedAction")));
  const auto link_row = unlinked_rows.indexOf(QStringLiteral("layerLinkAction"));
  CHECK(link_row > color_row && unlinked_rows.value(link_row - 1) == QStringLiteral("-"));
  const auto depth = Access::active_session_undo_depth(window);
  link->trigger();
  QApplication::processEvents();
  CHECK(Access::active_session_undo_depth(window) == depth + 1);
  CHECK(group_of(ids.top) != 0U && group_of(ids.top) == group_of(ids.middle));
  CHECK(group_of(ids.inner) == 0U && group_of(ids.background) == 0U);
  CHECK((linked_badge_rows(*list) == std::vector<patchy::LayerId>{std::min(ids.middle, ids.top),
                                                                  std::max(ids.middle, ids.top)}));
  // Now one link group: the same entry reads Unlink Layers.
  CHECK(link->isEnabled() && unlink->isEnabled() && select_linked->isEnabled());
  CHECK(link->text() == QStringLiteral("U&nlink Layers"));
  CHECK((Access::selected_layer_ids(window).size() == 2U));
  const auto linked_rows = context_menu_rows(window, ids.top, "issue656_fu_layer_context_menu");
  CHECK(linked_rows.count(QStringLiteral("layerLinkAction")) == 1);
  CHECK(!linked_rows.contains(QStringLiteral("layerUnlinkAction")));
  CHECK(linked_rows.indexOf(QStringLiteral("layerSelectLinkedAction")) ==
        linked_rows.indexOf(QStringLiteral("layerLinkAction")) + 1);
  // Triggered again, the toggle unlinks; undo brings the link back.
  const auto toggle_depth = Access::active_session_undo_depth(window);
  link->trigger();
  QApplication::processEvents();
  CHECK(Access::active_session_undo_depth(window) == toggle_depth + 1);
  CHECK(group_of(ids.top) == 0U && group_of(ids.middle) == 0U);
  CHECK(link->text() == QStringLiteral("&Link Layers"));
  Access::undo(window);
  QApplication::processEvents();
  Access::select_layers(window, {ids.top, ids.middle}, ids.top);
  QApplication::processEvents();
  CHECK(group_of(ids.top) != 0U && group_of(ids.top) == group_of(ids.middle));
  CHECK(link->text() == QStringLiteral("U&nlink Layers"));
  // A mixed selection (one linked, one not) links: the entry reads Link Layers.
  Access::select_layers(window, {ids.top, ids.inner}, ids.top);
  QApplication::processEvents();
  CHECK(link->isEnabled() && link->text() == QStringLiteral("&Link Layers"));
  Access::select_layers(window, {ids.top, ids.middle}, ids.top);
  QApplication::processEvents();

  for (const auto scheme : {patchy::ui::ColorSchemePreference::Dark, patchy::ui::ColorSchemePreference::Light}) {
    ColorSchemeRestorer restore(scheme);
    QApplication::processEvents();
    save_widget_artifact(scheme == patchy::ui::ColorSchemePreference::Dark ? "issue656_linked_rows_dark"
                                                                           : "issue656_linked_rows_light",
                         *list);
  }

  // Layer > Arrange with the commands, for the review shots.
  if (auto* layer_menu = window.findChild<QMenu*>(QStringLiteral("layerMenu")); layer_menu != nullptr) {
    layer_menu->popup(window.mapToGlobal(QPoint(120, 40)));
    QApplication::processEvents();
    arrange->popup(window.mapToGlobal(QPoint(420, 300)));
    QApplication::processEvents();
    save_widget_artifact("issue656_arrange_menu", *arrange);
    arrange->hide();
    layer_menu->hide();
    QApplication::processEvents();
  }

  // Select Linked Layers from one member selects the group; one linked layer alone reads Unlink.
  Access::select_layers(window, {ids.middle}, ids.middle);
  QApplication::processEvents();
  CHECK(select_linked->isEnabled() && link->isEnabled() && link->text() == QStringLiteral("U&nlink Layers"));
  select_linked->trigger();
  QApplication::processEvents();
  auto selected = Access::selected_layer_ids(window);
  std::sort(selected.begin(), selected.end());
  CHECK((selected == std::vector<patchy::LayerId>{std::min(ids.middle, ids.top), std::max(ids.middle, ids.top)}));

  // Undo and redo the link.
  Access::undo(window);
  QApplication::processEvents();
  CHECK(group_of(ids.top) == 0U && group_of(ids.middle) == 0U);
  CHECK(linked_badge_rows(*list).empty());
  Access::redo(window);
  QApplication::processEvents();
  CHECK(group_of(ids.top) != 0U && group_of(ids.top) == group_of(ids.middle));

  // The layer.unlink key: unlinking one of two leaves neither linked, one undo step.
  Access::select_layers(window, {ids.middle}, ids.middle);
  QApplication::processEvents();
  const auto unlink_depth = Access::active_session_undo_depth(window);
  unlink->trigger();
  QApplication::processEvents();
  CHECK(Access::active_session_undo_depth(window) == unlink_depth + 1);
  CHECK(group_of(ids.top) == 0U && group_of(ids.middle) == 0U);
  CHECK(linked_badge_rows(*list).empty());

  // The footer's link button: hidden by default, links a selection, then unlinks it.
  auto* button = window.findChild<QPushButton*>(QStringLiteral("layerLinkButton"));
  CHECK(button != nullptr);
  if (button == nullptr) {
    return;
  }
  CHECK(!button->isVisible());
  Access::select_layers(window, {ids.top, ids.inner}, ids.top);
  QApplication::processEvents();
  CHECK(button->isEnabled() && button->toolTip() == QStringLiteral("Link Layers"));
  button->click();
  QApplication::processEvents();
  CHECK(group_of(ids.top) != 0U && group_of(ids.top) == group_of(ids.inner));
  CHECK(button->toolTip() == QStringLiteral("Unlink Layers"));
  button->click();
  QApplication::processEvents();
  CHECK(group_of(ids.top) == 0U && group_of(ids.inner) == 0U);

  // Duplicate Layer on one linked layer copies the group; the copies link to each other only
  // (Photoshop 2026).
  Access::select_layers(window, {ids.top, ids.middle}, ids.top);
  QApplication::processEvents();
  link->trigger();
  QApplication::processEvents();
  const auto sources = group_of(ids.top);
  Access::select_layers(window, {ids.top}, ids.top);
  QApplication::processEvents();
  const auto before_count = patchy::layer_tree_count(layers());
  require_action(window, "layerDuplicateAction")->trigger();
  QApplication::processEvents();
  CHECK(patchy::layer_tree_count(layers()) == before_count + 2U);
  const auto copies = Access::selected_layer_ids(window);
  CHECK(copies.size() == 2U);
  if (copies.size() == 2U) {
    CHECK(group_of(copies[0]) != 0U && group_of(copies[0]) == group_of(copies[1]) && group_of(copies[0]) != sources);
  }
  CHECK(group_of(ids.top) == sources && group_of(ids.middle) == sources);
}

}  // namespace

std::vector<patchy::test::TestCase> layer_menu_commands_tests() {
  return {
      {"ui_layer_select_below_above_steps_rows", ui_layer_select_below_above_steps_rows},
      {"ui_layer_hide_show_layers_toggles_selection", ui_layer_hide_show_layers_toggles_selection},
      {"ui_layer_show_all_layers_shows_every_depth", ui_layer_show_all_layers_shows_every_depth},
      {"ui_layer_eye_tooltip_names_alt_click", ui_layer_eye_tooltip_names_alt_click},
      {"ui_layer_lock_choices_follow_the_selection", ui_layer_lock_choices_follow_the_selection},
      {"ui_layer_lock_layers_dialog_applies_and_undoes", ui_layer_lock_layers_dialog_applies_and_undoes},
      {"ui_layer_menu_hosts_export_lock_and_hide_entries", ui_layer_menu_hosts_export_lock_and_hide_entries},
      {"ui_layer_menu_groups_fill_and_merge_submenus", ui_layer_menu_groups_fill_and_merge_submenus},
      {"ui_issue_627_layer_menu_submenus", ui_issue_627_layer_menu_submenus},
      {"ui_issue_627_layer_menu_rasterize_submenu", ui_issue_627_layer_menu_rasterize_submenu},
      {"ui_layer_link_commands_link_unlink_undo_and_badges", ui_layer_link_commands_link_unlink_undo_and_badges},
  };
}
