// Customize Tool Palette (GitHub issues 213, 316 and 199; docs/tool-palette.md, "Customizing
// the palette"): hidden tools keep their keys, flyout members leave their menu and Shift+letter
// cycle, the two-column layout, wheel scrolling in a short window, the Layers footer's
// show/hide menu, the dialog's OK, Cancel and Reset to Default, and the stored settings.
#include "ui/app_settings.hpp"
#include "ui/canvas_widget.hpp"
#include "ui/main_window.hpp"
#include "ui/tool_palette_customization.hpp"
#include "ui/tool_palette_layout.hpp"
#include "test_harness.hpp"
#include "ui_test_access.hpp"
#include "ui_test_groups.hpp"
#include "ui_test_support.hpp"
#include "scripting_test_support.hpp"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QContextMenuEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLayout>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QRadioButton>
#include <QTabWidget>
#include <QTest>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QWheelEvent>

#include <functional>
#include <memory>
#include <string>
#include <vector>

using namespace patchy::test::ui;

namespace {

using patchy::ui::ToolPaletteCustomization;

// The stored customization keys, restored after the test (the window reads them when built).
struct CustomizationSettingsGuard {
  std::vector<std::unique_ptr<SettingsValueRestorer>> restorers;
  CustomizationSettingsGuard() {
    for (const auto* key : {"toolPalette/hiddenTools", "toolPalette/layout", "toolPalette/wheelScroll",
                            "toolPalette/showCustomizeButton", "layersPanel/hiddenFooterButtons"}) {
      restorers.push_back(std::make_unique<SettingsValueRestorer>(QString::fromLatin1(key)));
      patchy::ui::app_settings().remove(QString::fromLatin1(key));
    }
  }
};

QToolBar* tool_palette(patchy::ui::MainWindow& window) {
  auto* palette = window.findChild<QToolBar*>(QStringLiteral("toolPalette"));
  CHECK(palette != nullptr);
  return palette;
}

// On the palette and drawn: inside the bar (a stashed button is not) and not hidden.
bool on_palette(QToolBar& palette, const QWidget* button) {
  return button != nullptr && palette.isAncestorOf(button) && button->isVisibleTo(&palette);
}

QRect geometry_in_palette(QToolBar& palette, const QWidget& widget) {
  return {widget.mapTo(&palette, QPoint(0, 0)), widget.size()};
}

QToolButton* button_named(patchy::ui::MainWindow& window, const char* name) {
  auto* button = window.findChild<QToolButton*>(QLatin1String(name));
  CHECK(button != nullptr);
  return button;
}

void settle() {
  QApplication::processEvents();
  process_events_for(40);
}

// Shrinks the window until the palette is `shortage` px shorter than its tools need.
void shrink_palette(patchy::ui::MainWindow& window, QToolBar& palette, int shortage) {
  window.resize(window.width(), 1400);
  settle();
  const int chrome_height = window.height() - palette.height();
  window.resize(window.width(), palette.sizeHint().height() + chrome_height - shortage);
  settle();
}

void send_wheel(QWidget& target, int angle) {
  const QPointF center(target.width() / 2.0, target.height() / 3.0);
  QWheelEvent wheel(center, target.mapToGlobal(center), QPoint(), QPoint(0, angle), Qt::NoButton, Qt::NoModifier,
                    Qt::NoScrollPhase, false);
  QApplication::sendEvent(&target, &wheel);
  settle();
}

// Runs `act` on the Customize Tool Palette dialog once it is up (it runs its own loop).
void when_customize_dialog_opens(const std::function<void(QDialog&)>& act) {
  QTimer::singleShot(0, [act] {
    QDialog* dialog = nullptr;
    process_events_until([&] {
      dialog = find_top_level_dialog(QStringLiteral("toolPaletteCustomizeDialog"));
      return dialog != nullptr && dialog->isVisible();
    });
    CHECK(dialog != nullptr);
    if (dialog != nullptr) {
      act(*dialog);
    }
  });
}

QTreeWidgetItem* tree_row(QTreeWidget& tree, const QString& text) {
  const auto found = tree.findItems(text, Qt::MatchExactly | Qt::MatchRecursive, 0);
  CHECK(found.size() == 1);
  return found.isEmpty() ? nullptr : found.front();
}

void press_dialog_button(QDialog& dialog, QDialogButtonBox::StandardButton which) {
  auto* box = dialog.findChild<QDialogButtonBox*>(QStringLiteral("toolPaletteCustomizeButtonBox"));
  CHECK(box != nullptr);
  if (box != nullptr) {
    box->button(which)->click();
  }
}

// GitHub issue 213 (and 304's Hand tool): a hidden tool leaves the palette but keeps its key;
// a flyout member left out leaves its menu and its Shift+letter cycle, still selectable by its
// own command; hiding every member hides the slot, whose cycle then walks all of them.
void ui_tool_palette_customize_hidden_tools_keep_hotkeys() {
  CustomizationSettingsGuard settings_guard;
  auto&& [window, canvas] = ShownWindowFixture{};
  auto* palette = tool_palette(window);
  if (palette == nullptr) {
    return;
  }
  window.resize(window.width(), 1400);
  settle();
  auto* hand_button = button_named(window, "handToolButton");
  auto* healing_button = button_named(window, "healingToolButton");
  CHECK(on_palette(*palette, hand_button));
  CHECK(on_palette(*palette, button_named(window, "toolPaletteCustomizeButton")));
  const int full_height = palette->sizeHint().height();

  ToolPaletteCustomization customization;
  customization.hidden_tools = {QStringLiteral("tools.hand"), QStringLiteral("tools.red_eye")};
  window.apply_tool_palette_customization(customization);
  settle();
  CHECK(!on_palette(*palette, hand_button));
  CHECK(palette->sizeHint().height() < full_height);
  // H still selects the Hand tool.
  require_action(window, "toolBrushAction")->trigger();
  QTest::keyClick(canvas, Qt::Key_H);
  QApplication::processEvents();
  CHECK(canvas->tool() == patchy::ui::CanvasTool::Pan);
  CHECK(require_action(window, "toolHandAction")->isChecked());

  // Red Eye leaves the flyout and the Shift+J cycle; its command still selects it.
  auto* red_eye = require_action(window, "toolRedEyeAction");
  auto* cycle = require_action(window, "toolCycleHealingAction");
  CHECK(healing_button->menu() != nullptr && !healing_button->menu()->actions().contains(red_eye));
  CHECK(healing_button->menu()->actions().contains(cycle));
  CHECK(healing_button->menu()->actions().last() == cycle);
  require_action(window, "toolPatchAction")->trigger();
  QApplication::processEvents();
  QTest::keyClick(canvas, Qt::Key_J, Qt::ShiftModifier);
  QApplication::processEvents();
  CHECK(canvas->tool() == patchy::ui::CanvasTool::Healing);
  red_eye->trigger();
  QApplication::processEvents();
  CHECK(canvas->tool() == patchy::ui::CanvasTool::RedEye);
  CHECK(healing_button->defaultAction() != red_eye);

  // Every healing member hidden: the slot leaves the palette, Shift+J walks them all.
  customization.hidden_tools << QStringLiteral("tools.healing") << QStringLiteral("tools.spot_healing")
                             << QStringLiteral("tools.patch");
  window.apply_tool_palette_customization(customization);
  settle();
  CHECK(!on_palette(*palette, healing_button));
  QTest::keyClick(canvas, Qt::Key_J, Qt::ShiftModifier);
  QApplication::processEvents();
  CHECK(canvas->tool() == patchy::ui::CanvasTool::Healing);
  QTest::keyClick(canvas, Qt::Key_J, Qt::ShiftModifier);
  QTest::keyClick(canvas, Qt::Key_J, Qt::ShiftModifier);
  QTest::keyClick(canvas, Qt::Key_J, Qt::ShiftModifier);
  QApplication::processEvents();
  CHECK(canvas->tool() == patchy::ui::CanvasTool::RedEye);

  // Back to the defaults: everything returns, Red Eye last in its flyout again.
  window.apply_tool_palette_customization(ToolPaletteCustomization{});
  settle();
  CHECK(on_palette(*palette, hand_button));
  CHECK(on_palette(*palette, healing_button));
  CHECK(healing_button->menu()->actions().indexOf(red_eye) == 3);
  CHECK(palette->sizeHint().height() == full_height);
}

// philipzae's horizontal layout (GitHub issue 213): two tools side by side, left to right
// within each cluster, and the overflow arrow still opens more columns of whole rows.
void ui_tool_palette_two_columns_place_tools_side_by_side() {
  CustomizationSettingsGuard settings_guard;
  auto&& [window, canvas] = ShownWindowFixture{};
  auto* palette = tool_palette(window);
  if (palette == nullptr) {
    return;
  }
  ToolPaletteCustomization customization;
  customization.two_columns = true;
  customization.wheel_scroll = false;
  window.apply_tool_palette_customization(customization);
  window.resize(window.width(), 1200);
  settle();
  auto* move = button_named(window, "moveToolButton");
  auto* marquee = button_named(window, "marqueeToolButton");
  auto* lasso = button_named(window, "lassoToolButton");
  auto* brush = button_named(window, "brushToolButton");
  auto* eraser = button_named(window, "eraserToolButton");
  auto* gradient = button_named(window, "gradientToolButton");
  for (auto* button : {move, marquee, lasso, brush, eraser, gradient}) {
    CHECK(on_palette(*palette, button));
  }
  const auto move_rect = geometry_in_palette(*palette, *move);
  const auto marquee_rect = geometry_in_palette(*palette, *marquee);
  CHECK(marquee_rect.top() == move_rect.top());
  CHECK(marquee_rect.left() > move_rect.right());
  CHECK(geometry_in_palette(*palette, *lasso).top() > move_rect.bottom());
  CHECK(geometry_in_palette(*palette, *lasso).left() == move_rect.left());
  // Clusters start a new row: Brush begins the paint cluster in the left column.
  CHECK(geometry_in_palette(*palette, *brush).left() == move_rect.left());
  CHECK(geometry_in_palette(*palette, *eraser).top() == geometry_in_palette(*palette, *brush).top());
  CHECK(palette->width() >= 2 * move->width());
  marquee->click();
  QApplication::processEvents();
  CHECK(canvas->tool() == patchy::ui::CanvasTool::Marquee);
  (void)capture_widget_artifact("ui_tool_palette_two_columns", *palette);

  // A short window folds the bottom rows into the extension, which opens more columns. Two
  // columns are short, so the window (whose panels keep it from getting very short) uses the
  // Biggest toolbar icon size; it reads the stored two-column choice.
  SettingsValueRestorer icon_size(QStringLiteral("preferences/toolbarIconSize"));
  patchy::ui::app_settings().setValue(QStringLiteral("preferences/toolbarIconSize"), QStringLiteral("biggest"));
  patchy::ui::MainWindow big;
  show_window(big);
  auto* big_palette = tool_palette(big);
  if (big_palette == nullptr) {
    return;
  }
  CHECK(big.tool_palette_customization() == customization);
  shrink_palette(big, *big_palette, 120);
  CHECK(big_palette->sizeHint().height() > big_palette->height());
  auto* extension = big_palette->findChild<QToolButton*>(QStringLiteral("qt_toolbar_ext_button"));
  auto* zoom = button_named(big, "zoomToolButton");
  auto* big_move = button_named(big, "moveToolButton");
  CHECK(extension != nullptr && extension->isVisible());
  CHECK(!zoom->isVisible());
  if (extension != nullptr) {
    extension->click();
    settle();
    CHECK(zoom->isVisible());
    CHECK(big_palette->width() > 3 * big_move->width());
    extension->click();
    settle();
    CHECK(!zoom->isVisible());
  }
}

// GitHub issue 316: in a short window the wheel scrolls the column; arrows mark more above and
// below; the color well stays pinned; a taller window brings the top back. With the option off
// the > arrow (the overflow extension) works as before.
void ui_tool_palette_wheel_scrolls_overflowing_column() {
  CustomizationSettingsGuard settings_guard;
  auto&& [window, canvas] = ShownWindowFixture{};
  auto* palette = tool_palette(window);
  auto* layout = window.tool_palette_layout();
  CHECK(layout != nullptr);
  if (palette == nullptr || layout == nullptr) {
    return;
  }
  CHECK(window.tool_palette_customization().wheel_scroll);
  shrink_palette(window, *palette, 150);
  auto* down = palette->findChild<QWidget*>(QStringLiteral("toolPaletteScrollDownArrow"));
  auto* up = palette->findChild<QWidget*>(QStringLiteral("toolPaletteScrollUpArrow"));
  auto* extension = palette->findChild<QToolButton*>(QStringLiteral("qt_toolbar_ext_button"));
  auto* footer = palette->findChild<QWidget*>(QStringLiteral("toolPaletteFooter"));
  auto* zoom = button_named(window, "zoomToolButton");
  auto* move = button_named(window, "moveToolButton");
  CHECK(down != nullptr && up != nullptr && extension != nullptr && footer != nullptr);
  if (down == nullptr || up == nullptr || extension == nullptr || footer == nullptr) {
    return;
  }
  CHECK(down->isVisible());
  CHECK(!up->isVisible());
  // The arrow covers the extension, so the > arrow cannot open columns in this mode.
  CHECK(down->geometry().contains(extension->geometry().center()));
  CHECK(!zoom->isVisible());
  CHECK(layout->can_scroll_down());

  int notches = 0;
  while (!zoom->isVisible() && notches < 30) {
    send_wheel(*palette, -120);
    ++notches;
  }
  CHECK(zoom->isVisible());
  CHECK(layout->scroll_offset() > 0);
  CHECK(up->isVisible());
  CHECK(!move->isVisible());
  CHECK(footer->geometry().bottom() >= palette->height() - 6);
  CHECK(geometry_in_palette(*palette, *zoom).bottom() < footer->geometry().top());
  (void)capture_widget_artifact("ui_tool_palette_wheel_scrolled", *palette);
  for (int step = 0; step < 30 && layout->scroll_offset() > 0; ++step) {
    send_wheel(*palette, 120);
  }
  CHECK(layout->scroll_offset() == 0);
  CHECK(move->isVisible());
  CHECK(!up->isVisible());

  // A taller window brings the scrolled-off tools back.
  for (int step = 0; step < 3; ++step) {
    send_wheel(*palette, -120);
  }
  CHECK(layout->scroll_offset() > 0);
  window.resize(window.width(), 1400);
  settle();
  CHECK(layout->scroll_offset() == 0);
  CHECK(move->isVisible());
  CHECK(zoom->isVisible());
  CHECK(!down->isVisible());

  // Option off: the extension takes over again and the wheel scrolls nothing.
  auto customization = window.tool_palette_customization();
  customization.wheel_scroll = false;
  window.apply_tool_palette_customization(customization);
  shrink_palette(window, *palette, 150);
  CHECK(extension->isVisible());
  CHECK(!down->isVisible());
  send_wheel(*palette, -120);
  CHECK(layout->scroll_offset() == 0);
  CHECK(move->isVisible());
}

// GitHub issue 199: a right-click on any Layers footer button lists the buttons with a tick
// each; an unticked button leaves the footer and is stored; Show All Buttons brings every one
// back; the last one shown cannot be unticked.
void ui_layer_footer_buttons_hide_from_their_menu() {
  CustomizationSettingsGuard settings_guard;
  auto&& [window, canvas] = ShownWindowFixture{};
  auto* animation = window.findChild<QPushButton*>(QStringLiteral("layerAnimationButton"));
  auto* duplicate = window.findChild<QPushButton*>(QStringLiteral("layerDuplicateButton"));
  CHECK(animation != nullptr && duplicate != nullptr);
  if (animation == nullptr || duplicate == nullptr) {
    return;
  }
  CHECK(animation->isVisible());
  const auto open_menu = [&](QWidget& target) -> QMenu* {
    const QPoint position = target.rect().center();
    QContextMenuEvent event(QContextMenuEvent::Mouse, position, target.mapToGlobal(position));
    QApplication::sendEvent(&target, &event);
    QApplication::processEvents();
    auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
    CHECK(menu != nullptr && menu->objectName() == QStringLiteral("layerFooterButtonsMenu"));
    return menu;
  };
  auto* menu = open_menu(*duplicate);
  if (menu == nullptr) {
    return;
  }
  auto* toggle = menu->findChild<QAction*>(QStringLiteral("layerFooterToggle_animation"));
  CHECK(toggle != nullptr && toggle->isChecked());
  CHECK(menu->findChild<QAction*>(QStringLiteral("layerFooterShowAllAction")) != nullptr);
  CHECK(menu->findChild<QAction*>(QStringLiteral("layerFooterCustomizeAction")) != nullptr);
  CHECK(menu->actions().size() == 8 + 3);
  (void)capture_widget_artifact("ui_layer_footer_buttons_menu", *menu);
  if (toggle != nullptr) {
    toggle->trigger();
  }
  menu->close();
  settle();
  CHECK(!animation->isVisible());
  CHECK(window.tool_palette_customization().hidden_layer_buttons == QStringList{QStringLiteral("animation")});
  CHECK(patchy::ui::stored_tool_palette_customization().hidden_layer_buttons ==
        QStringList{QStringLiteral("animation")});
  CHECK(duplicate->isVisible());
  auto* layers_panel = duplicate->parentWidget();
  if (layers_panel != nullptr) {
    (void)capture_widget_artifact("ui_layer_footer_without_animation", *layers_panel);
  }

  // Show All Buttons.
  menu = open_menu(*duplicate);
  if (menu != nullptr) {
    menu->findChild<QAction*>(QStringLiteral("layerFooterShowAllAction"))->trigger();
    menu->close();
  }
  settle();
  CHECK(animation->isVisible());
  CHECK(window.tool_palette_customization().hidden_layer_buttons.isEmpty());

  // Only one button left: its tick cannot be removed.
  ToolPaletteCustomization customization;
  for (const auto* id : patchy::ui::kLayerFooterButtonIds) {
    if (QLatin1String(id) != QLatin1String("delete")) {
      customization.hidden_layer_buttons.push_back(QString::fromLatin1(id));
    }
  }
  window.apply_tool_palette_customization(customization);
  settle();
  auto* delete_button = window.findChild<QPushButton*>(QStringLiteral("layerDeleteButton"));
  CHECK(delete_button != nullptr && delete_button->isVisible());
  CHECK(!duplicate->isVisible());
  if (delete_button != nullptr) {
    menu = open_menu(*delete_button);
    if (menu != nullptr) {
      CHECK(!menu->findChild<QAction*>(QStringLiteral("layerFooterToggle_delete"))->isEnabled());
      CHECK(menu->findChild<QAction*>(QStringLiteral("layerFooterToggle_duplicate"))->isEnabled());
      menu->close();
    }
  }
  window.apply_tool_palette_customization(ToolPaletteCustomization{});
  settle();
  CHECK(duplicate->isVisible());
}

// The dialog (mockup 1): ticks change the preview, OK applies and stores, Cancel changes
// nothing, Reset to Default restores everything and clears the stored keys. The gear, the
// Window menu and the palette's right-click menu open it; a new window reads the stored
// choice.
void ui_tool_palette_customize_dialog_applies_cancels_and_resets() {
  CustomizationSettingsGuard settings_guard;
  auto&& [window, canvas] = ShownWindowFixture{};
  auto* palette = tool_palette(window);
  if (palette == nullptr) {
    return;
  }
  window.resize(window.width(), 1400);
  settle();
  auto* gear = button_named(window, "toolPaletteCustomizeButton");
  auto* window_menu_action = require_action(window, "windowCustomizeToolPaletteAction");
  CHECK(gear->defaultAction() == window_menu_action);

  // Untick Hand and Red Eye, pick two columns, OK.
  bool ran = false;
  when_customize_dialog_opens([&](QDialog& dialog) {
    ran = true;
    auto* tree = dialog.findChild<QTreeWidget*>(QStringLiteral("toolPaletteCustomizeTree"));
    auto* preview = dialog.findChild<QWidget*>(QStringLiteral("toolPaletteCustomizePreview"));
    CHECK(tree != nullptr && preview != nullptr);
    if (tree == nullptr || preview == nullptr) {
      dialog.reject();
      return;
    }
    const int preview_height = preview->height();
    if (auto* hand = tree_row(*tree, QStringLiteral("Hand")); hand != nullptr) {
      CHECK(hand->checkState(0) == Qt::Checked);
      hand->setCheckState(0, Qt::Unchecked);
    }
    QApplication::processEvents();
    CHECK(preview->height() < preview_height);
    if (auto* red_eye = tree_row(*tree, QStringLiteral("Red Eye")); red_eye != nullptr) {
      red_eye->setCheckState(0, Qt::Unchecked);
      // A partly hidden flyout shows a partial check.
      CHECK(red_eye->parent() != nullptr && red_eye->parent()->checkState(0) == Qt::PartiallyChecked);
      red_eye->parent()->setExpanded(true);
    }
    (void)capture_widget_artifact("ui_tool_palette_customize_dialog", dialog);
    const int one_column_width = preview->width();
    dialog.findChild<QRadioButton*>(QStringLiteral("toolPaletteTwoColumnsRadio"))->setChecked(true);
    QApplication::processEvents();
    CHECK(preview->width() > one_column_width);
    press_dialog_button(dialog, QDialogButtonBox::Ok);
  });
  gear->click();
  CHECK(ran);
  settle();
  auto expected = ToolPaletteCustomization{};
  expected.hidden_tools = {QStringLiteral("tools.hand"), QStringLiteral("tools.red_eye")};
  expected.two_columns = true;
  CHECK(window.tool_palette_customization() == expected);
  CHECK(patchy::ui::stored_tool_palette_customization() == expected);
  CHECK(!on_palette(*palette, button_named(window, "handToolButton")));

  // A new window starts with the stored choice.
  {
    patchy::ui::MainWindow second;
    CHECK(second.tool_palette_customization() == expected);
    auto* second_palette = second.findChild<QToolBar*>(QStringLiteral("toolPalette"));
    auto* second_hand = second.findChild<QToolButton*>(QStringLiteral("handToolButton"));
    CHECK(second_palette != nullptr && second_hand != nullptr && !second_palette->isAncestorOf(second_hand));
  }

  // Cancel keeps the choice, whatever was ticked.
  ran = false;
  when_customize_dialog_opens([&](QDialog& dialog) {
    ran = true;
    dialog.findChild<QCheckBox*>(QStringLiteral("toolPaletteShowGearCheck"))->setChecked(false);
    press_dialog_button(dialog, QDialogButtonBox::Cancel);
  });
  window_menu_action->trigger();
  CHECK(ran);
  settle();
  CHECK(window.tool_palette_customization() == expected);

  // Reset to Default restores everything, OK stores it as no keys at all.
  ran = false;
  when_customize_dialog_opens([&](QDialog& dialog) {
    ran = true;
    auto* layer_list = dialog.findChild<QListWidget*>(QStringLiteral("layerFooterButtonsList"));
    CHECK(layer_list != nullptr && layer_list->count() == 8);
    if (layer_list != nullptr && layer_list->count() > 0) {
      layer_list->item(0)->setCheckState(Qt::Unchecked);
    }
    dialog.findChild<QPushButton*>(QStringLiteral("toolPaletteCustomizeResetButton"))->click();
    QApplication::processEvents();
    auto* tree = dialog.findChild<QTreeWidget*>(QStringLiteral("toolPaletteCustomizeTree"));
    if (tree != nullptr) {
      if (auto* hand = tree_row(*tree, QStringLiteral("Hand")); hand != nullptr) {
        CHECK(hand->checkState(0) == Qt::Checked);
      }
    }
    CHECK(dialog.findChild<QRadioButton*>(QStringLiteral("toolPaletteOneColumnRadio"))->isChecked());
    CHECK(layer_list == nullptr || layer_list->item(0)->checkState() == Qt::Checked);
    press_dialog_button(dialog, QDialogButtonBox::Ok);
  });
  window.show_tool_palette_customization();
  CHECK(ran);
  settle();
  CHECK(window.tool_palette_customization() == ToolPaletteCustomization{});
  {
    auto settings = patchy::ui::app_settings();
    for (const auto* key : {"toolPalette/hiddenTools", "toolPalette/layout", "toolPalette/wheelScroll",
                            "toolPalette/showCustomizeButton", "layersPanel/hiddenFooterButtons"}) {
      CHECK(!settings.contains(QString::fromLatin1(key)));
    }
  }
  CHECK(on_palette(*palette, button_named(window, "handToolButton")));

  // The palette's right-click menu leads with Customize Tool Palette...; the gear can be hidden.
  const QPoint position(palette->width() / 2, palette->height() - 2);
  QContextMenuEvent event(QContextMenuEvent::Mouse, position, palette->mapToGlobal(position));
  QApplication::sendEvent(palette, &event);
  QApplication::processEvents();
  auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
  CHECK(menu != nullptr && !menu->actions().isEmpty() && menu->actions().front() == window_menu_action);
  if (menu != nullptr) {
    menu->close();
  }
  auto hidden_gear = ToolPaletteCustomization{};
  hidden_gear.show_customize_button = false;
  window.apply_tool_palette_customization(hidden_gear);
  settle();
  CHECK(!gear->isVisible());
  window.apply_tool_palette_customization(ToolPaletteCustomization{});
  settle();
  CHECK(gear->isVisible());
}

// patchy.ui.toolPalette reads and sets the choice (any subset of keys), applies it live and
// saves it; unknown ids or layouts throw and change nothing.
void ui_script_tool_palette_reads_and_sets() {
  using patchy::test::ui::scripting_support::backlog_contains;
  using patchy::test::ui::scripting_support::run_script;
  CustomizationSettingsGuard settings_guard;
  auto&& [window, canvas] = ShownWindowFixture{};
  auto* palette = tool_palette(window);
  if (palette == nullptr) {
    return;
  }
  CHECK(run_script(window, QStringLiteral("const p = patchy.ui.toolPalette;"
                                          "console.log('layout=' + p.layout + ' wheel=' + p.wheelScroll + "
                                          "' gear=' + p.showGearButton + ' hidden=' + p.hiddenTools.length);")));
  CHECK(backlog_contains(window, QStringLiteral("layout=oneColumn wheel=true gear=true hidden=0")));
  CHECK(run_script(window, QStringLiteral("patchy.ui.toolPalette = {hiddenTools: ['tools.hand', 'tools.ruler'],"
                                          " layout: 'twoColumns', hiddenLayerButtons: ['animation']};"
                                          "console.log('now=' + patchy.ui.toolPalette.hiddenTools.join(','));")));
  CHECK(backlog_contains(window, QStringLiteral("now=tools.hand,tools.ruler")));
  settle();
  auto expected = ToolPaletteCustomization{};
  expected.hidden_tools = {QStringLiteral("tools.hand"), QStringLiteral("tools.ruler")};
  expected.two_columns = true;
  expected.hidden_layer_buttons = {QStringLiteral("animation")};
  CHECK(window.tool_palette_customization() == expected);
  CHECK(patchy::ui::stored_tool_palette_customization() == expected);
  CHECK(!on_palette(*palette, button_named(window, "handToolButton")));
  // A partial assignment keeps the other keys.
  CHECK(run_script(window, QStringLiteral("patchy.ui.toolPalette = {wheelScroll: false};")));
  expected.wheel_scroll = false;
  CHECK(window.tool_palette_customization() == expected);
  // Unknown values throw and change nothing.
  CHECK(run_script(window, QStringLiteral("try { patchy.ui.toolPalette = {hiddenTools: ['tools.nope']}; }"
                                          " catch (e) { console.log('threw tool'); }"
                                          "try { patchy.ui.toolPalette = {layout: 'three'}; }"
                                          " catch (e) { console.log('threw layout'); }")));
  CHECK(backlog_contains(window, QStringLiteral("threw tool")));
  CHECK(backlog_contains(window, QStringLiteral("threw layout")));
  CHECK(window.tool_palette_customization() == expected);
  window.apply_tool_palette_customization(ToolPaletteCustomization{});
}

}  // namespace

std::vector<patchy::test::TestCase> tool_palette_customize_tests() {
  return {
      {"ui_tool_palette_customize_hidden_tools_keep_hotkeys", ui_tool_palette_customize_hidden_tools_keep_hotkeys},
      {"ui_tool_palette_two_columns_place_tools_side_by_side",
       ui_tool_palette_two_columns_place_tools_side_by_side},
      {"ui_tool_palette_wheel_scrolls_overflowing_column", ui_tool_palette_wheel_scrolls_overflowing_column},
      {"ui_layer_footer_buttons_hide_from_their_menu", ui_layer_footer_buttons_hide_from_their_menu},
      {"ui_tool_palette_customize_dialog_applies_cancels_and_resets",
       ui_tool_palette_customize_dialog_applies_cancels_and_resets},
      {"ui_script_tool_palette_reads_and_sets", ui_script_tool_palette_reads_and_sets},
  };
}
