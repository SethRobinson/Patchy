// Customize Tool Palette (GitHub issues 213, 316 and 199; docs/tool-palette.md, "Customizing
// the palette"): hidden tools keep their keys, flyout members leave their menu and Shift+letter
// cycle, the two-column layout, wheel scrolling in a short window, the Layers footer's
// show/hide menu, the dialog's OK, Cancel and Reset to Default, and the stored settings. Also
// the palette's bottom items, the toolbar icon size and tool group opening shown in the dialog
// (213 follow-up), and when tool groups open: click and hold, click or hover (459).
#include "ui/app_settings.hpp"
#include "ui/canvas_widget.hpp"
#include "ui/main_window.hpp"
#include "ui/tool_palette_customization.hpp"
#include "ui/tool_palette_footer.hpp"
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
#include <QComboBox>
#include <QContextMenuEvent>
#include <QCursor>
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
#include <QTreeWidgetItemIterator>
#include <QWheelEvent>

#include <cstdio>
#include <functional>
#include <iterator>
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
                            "toolPalette/showCustomizeButton", "layersPanel/hiddenFooterButtons",
                            "toolPalette/hiddenFooterItems", "preferences/toolbarIconSize",
                            "preferences/toolFlyoutOpenMode"}) {
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
  // No gear: the palette starts with Move, as before Customize Tool Palette.
  CHECK(window.findChild<QToolButton*>(QStringLiteral("toolPaletteCustomizeButton")) == nullptr);
  auto* move_button = button_named(window, "moveToolButton");
  CHECK(on_palette(*palette, move_button));
  for (auto* button : palette->findChildren<QToolButton*>()) {
    if (button != move_button && button->isVisibleTo(palette) && button->defaultAction() != nullptr &&
        button->defaultAction()->property(patchy::ui::kToolPaletteToolIdProperty).isValid()) {
      CHECK(geometry_in_palette(*palette, *button).top() > geometry_in_palette(*palette, *move_button).top());
    }
  }
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
// nothing, Reset to Default restores everything and clears the stored keys. The Window menu
// and the palette's right-click menu open it; a new window reads the stored choice.
void ui_tool_palette_customize_dialog_applies_cancels_and_resets() {
  CustomizationSettingsGuard settings_guard;
  auto&& [window, canvas] = ShownWindowFixture{};
  auto* palette = tool_palette(window);
  if (palette == nullptr) {
    return;
  }
  window.resize(window.width(), 1400);
  settle();
  auto* window_menu_action = require_action(window, "windowCustomizeToolPaletteAction");

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
    // Rows have room (GitHub issue 213 follow-up): a tool row and a member row at least 24 px,
    // a cluster heading taller than a row.
    if (auto* hand = tree_row(*tree, QStringLiteral("Hand")); hand != nullptr) {
      CHECK(tree->visualItemRect(hand).height() >= 24);
      CHECK(tree->visualItemRect(hand->parent()).height() > tree->visualItemRect(hand).height());
    }
    if (auto* patch = tree_row(*tree, QStringLiteral("Patch")); patch != nullptr) {
      CHECK(tree->visualItemRect(patch).height() >= 24);
    }
    (void)capture_widget_artifact("ui_tool_palette_customize_dialog", dialog);
    const int one_column_width = preview->width();
    dialog.findChild<QRadioButton*>(QStringLiteral("toolPaletteTwoColumnsRadio"))->setChecked(true);
    QApplication::processEvents();
    CHECK(preview->width() > one_column_width);
    press_dialog_button(dialog, QDialogButtonBox::Ok);
  });
  window_menu_action->trigger();
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
    CHECK(dialog.findChild<QCheckBox*>(QStringLiteral("toolPaletteShowGearCheck")) == nullptr);
    dialog.findChild<QCheckBox*>(QStringLiteral("toolPaletteWheelScrollCheck"))->setChecked(false);
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

  // The palette's right-click menu leads with Customize Tool Palette....
  const QPoint position(palette->width() / 2, palette->height() - 2);
  QContextMenuEvent event(QContextMenuEvent::Mouse, position, palette->mapToGlobal(position));
  QApplication::sendEvent(palette, &event);
  QApplication::processEvents();
  auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
  CHECK(menu != nullptr && !menu->actions().isEmpty() && menu->actions().front() == window_menu_action);
  if (menu != nullptr) {
    menu->close();
  }
}

// GitHub issue 213 follow-up: settings written by an earlier version name only what was hidden
// then, so a tool they do not mention (one added later) shows, in its flyout too, even in a
// flyout whose stored members are all hidden; ids this build does not know are ignored and
// kept. The Layers footer works the same way.
void ui_tool_palette_settings_from_an_older_version_show_new_tools() {
  CustomizationSettingsGuard settings_guard;
  {
    auto settings = patchy::ui::app_settings();
    // Pick hidden, Ruler (the "new" tool) not named; plus ids from some other version.
    settings.setValue(QStringLiteral("toolPalette/hiddenTools"),
                      QStringList{QStringLiteral("tools.eyedropper"), QStringLiteral("tools.hand"),
                                  QStringLiteral("tools.not_yet_invented")});
    settings.setValue(QStringLiteral("layersPanel/hiddenFooterButtons"),
                      QStringList{QStringLiteral("animation"), QStringLiteral("future_button")});
  }
  auto&& [window, canvas] = ShownWindowFixture{};
  auto* palette = tool_palette(window);
  if (palette == nullptr) {
    return;
  }
  window.resize(window.width(), 1400);
  settle();
  auto* pick_button = button_named(window, "pickToolButton");
  auto* ruler = require_action(window, "toolRulerAction");
  auto* pick = require_action(window, "toolPickAction");
  CHECK(on_palette(*palette, pick_button));
  CHECK(pick_button->defaultAction() == ruler);
  CHECK(pick_button->menu() != nullptr && pick_button->menu()->actions().contains(ruler));
  CHECK(!pick_button->menu()->actions().contains(pick));
  CHECK(!on_palette(*palette, button_named(window, "handToolButton")));
  // Shift+I walks the visible members: only Ruler.
  require_action(window, "toolBrushAction")->trigger();
  QTest::keyClick(canvas, Qt::Key_I, Qt::ShiftModifier);
  QApplication::processEvents();
  CHECK(canvas->tool() == patchy::ui::CanvasTool::Ruler);
  // Every other slot shows.
  auto* layout = window.tool_palette_layout();
  CHECK(layout != nullptr);
  if (layout != nullptr) {
    for (const auto& slot : layout->entries()) {
      if (slot.button != button_named(window, "handToolButton")) {
        CHECK(on_palette(*palette, slot.button));
      }
    }
  }
  // Unknown ids are kept, never an error; the footer hides only what it knows.
  CHECK(window.tool_palette_customization().hidden_tools.contains(QStringLiteral("tools.not_yet_invented")));
  CHECK(window.tool_palette_customization().hidden_layer_buttons.contains(QStringLiteral("future_button")));
  auto* animation = window.findChild<QPushButton*>(QStringLiteral("layerAnimationButton"));
  auto* duplicate = window.findChild<QPushButton*>(QStringLiteral("layerDuplicateButton"));
  CHECK(animation != nullptr && !animation->isVisible());
  CHECK(duplicate != nullptr && duplicate->isVisible());
  // OK in the dialog writes the unknown ids back.
  bool ran = false;
  when_customize_dialog_opens([&](QDialog& dialog) {
    ran = true;
    press_dialog_button(dialog, QDialogButtonBox::Ok);
  });
  window.show_tool_palette_customization();
  CHECK(ran);
  settle();
  const auto stored = patchy::ui::stored_tool_palette_customization();
  CHECK(stored.hidden_tools.contains(QStringLiteral("tools.not_yet_invented")));
  CHECK(stored.hidden_tools.contains(QStringLiteral("tools.eyedropper")));
  CHECK(!stored.hidden_tools.contains(QStringLiteral("tools.ruler")));
  CHECK(stored.hidden_layer_buttons.contains(QStringLiteral("future_button")));
}

// The dialog's rows come from the palette itself: every tool the palette holds has a row, and
// every Layers footer button an entry, so a tool added later needs no list of its own.
void ui_tool_palette_dialog_lists_every_palette_tool() {
  CustomizationSettingsGuard settings_guard;
  auto&& [window, canvas] = ShownWindowFixture{};
  QStringList palette_tools;
  for (const auto* action : window.findChildren<QAction*>()) {
    if (const auto id = action->property(patchy::ui::kToolPaletteToolIdProperty).toString(); !id.isEmpty()) {
      palette_tools.push_back(id);
    }
  }
  palette_tools.removeDuplicates();
  CHECK(palette_tools.size() >= 40);
  bool ran = false;
  when_customize_dialog_opens([&](QDialog& dialog) {
    ran = true;
    auto* tree = dialog.findChild<QTreeWidget*>(QStringLiteral("toolPaletteCustomizeTree"));
    auto* layer_list = dialog.findChild<QListWidget*>(QStringLiteral("layerFooterButtonsList"));
    CHECK(tree != nullptr && layer_list != nullptr);
    if (tree != nullptr) {
      QStringList rows;
      for (QTreeWidgetItemIterator it(tree); *it != nullptr; ++it) {
        if (const auto id = (*it)->data(0, Qt::UserRole).toString(); !id.isEmpty()) {
          rows.push_back(id);
        }
      }
      for (const auto& id : palette_tools) {
        if (!rows.contains(id)) {
          std::fprintf(stderr, "no dialog row for %s\n", qPrintable(id));
        }
        CHECK(rows.contains(id));
      }
      CHECK(rows.size() == palette_tools.size());
    }
    if (layer_list != nullptr) {
      CHECK(layer_list->count() == static_cast<int>(std::size(patchy::ui::kLayerFooterButtonIds)));
    }
    press_dialog_button(dialog, QDialogButtonBox::Cancel);
  });
  window.show_tool_palette_customization();
  CHECK(ran);
}

// Preferences > Application has a Customize Tool Palette... button under Toolbar icon size; the
// dialog opens as Preferences' child.
void ui_preferences_opens_customize_tool_palette() {
  CustomizationSettingsGuard settings_guard;
  auto&& [window, canvas] = ShownWindowFixture{};
  bool saw_preferences = false;
  bool saw_customize = false;
  QTimer::singleShot(0, [&] {
    QDialog* preferences = nullptr;
    process_events_until([&] {
      preferences = find_top_level_dialog(QStringLiteral("patchyPreferencesDialog"));
      return preferences != nullptr && preferences->isVisible();
    });
    CHECK(preferences != nullptr);
    if (preferences == nullptr) {
      return;
    }
    saw_preferences = true;
    auto* button = preferences->findChild<QPushButton*>(QStringLiteral("preferencesCustomizeToolPaletteButton"));
    auto* size_combo = preferences->findChild<QWidget*>(QStringLiteral("preferencesToolbarIconSizeCombo"));
    CHECK(button != nullptr && size_combo != nullptr);
    if (button == nullptr || size_combo == nullptr) {
      preferences->reject();
      return;
    }
    // On the line under Interface scale and Toolbar icon size, lined up with their fields.
    const auto button_top = button->mapTo(preferences, QPoint(0, 0));
    const auto combo_top = size_combo->mapTo(preferences, QPoint(0, 0));
    CHECK(button_top.y() > combo_top.y());
    if (auto* pages = preferences->findChild<QListWidget*>(QStringLiteral("preferencesPageList")); pages != nullptr) {
      pages->setCurrentRow(0);
      QApplication::processEvents();
    }
    (void)capture_widget_artifact("ui_preferences_customize_tool_palette_button", *preferences);
    // When tool groups open (GitHub issue 459): its own row, Click and hold by default.
    auto* open_combo = preferences->findChild<QComboBox*>(QStringLiteral("preferencesToolFlyoutOpenCombo"));
    CHECK(open_combo != nullptr && open_combo->currentData().toString() == QStringLiteral("hold"));
    CHECK(open_combo == nullptr || open_combo->count() == 3);
    when_customize_dialog_opens([&, preferences, open_combo](QDialog& dialog) {
      saw_customize = true;
      CHECK(dialog.parentWidget() == preferences);
      // The dialog shows the same two settings; OK there moves Preferences' combos too.
      auto* dialog_open = dialog.findChild<QComboBox*>(QStringLiteral("toolPaletteFlyoutOpenCombo"));
      auto* dialog_size = dialog.findChild<QComboBox*>(QStringLiteral("toolPaletteIconSizeCombo"));
      CHECK(dialog_open != nullptr && dialog_size != nullptr);
      if (dialog_open != nullptr && dialog_size != nullptr) {
        CHECK(dialog_size->currentData().toString() == QStringLiteral("normal"));
        dialog_open->setCurrentIndex(dialog_open->findData(QStringLiteral("click")));
        dialog_size->setCurrentIndex(dialog_size->findData(QStringLiteral("bigger")));
      }
      press_dialog_button(dialog, QDialogButtonBox::Ok);
      QTimer::singleShot(0, preferences, [preferences, open_combo] {
        auto* size = preferences->findChild<QComboBox*>(QStringLiteral("preferencesToolbarIconSizeCombo"));
        CHECK(open_combo == nullptr || open_combo->currentData().toString() == QStringLiteral("click"));
        CHECK(size != nullptr && size->currentData().toString() == QStringLiteral("bigger"));
        // Preferences' OK keeps what the dialog chose.
        preferences->accept();
      });
    });
    button->click();
  });
  patchy::ui::MainWindowTestAccess::show_preferences(window);
  settle();
  CHECK(saw_preferences);
  CHECK(saw_customize);
  CHECK(window.tool_flyout_open_mode() == patchy::ui::ToolFlyoutOpenMode::Click);
  CHECK(patchy::ui::stored_toolbar_icon_size() == patchy::ui::ToolbarIconSize::Bigger);
  CHECK(patchy::ui::app_settings().value(QStringLiteral("preferences/toolFlyoutOpenMode")).toString() ==
        QStringLiteral("click"));

  // The other way: Preferences' OK moves an open customize dialog's combos.
  bool synced = false;
  when_customize_dialog_opens([&](QDialog& dialog) {
    window.apply_tool_palette_preferences(patchy::ui::ToolbarIconSize::Normal, patchy::ui::ToolFlyoutOpenMode::Hover);
    auto* dialog_open = dialog.findChild<QComboBox*>(QStringLiteral("toolPaletteFlyoutOpenCombo"));
    auto* dialog_size = dialog.findChild<QComboBox*>(QStringLiteral("toolPaletteIconSizeCombo"));
    synced = dialog_open != nullptr && dialog_open->currentData().toString() == QStringLiteral("hover") &&
             dialog_size != nullptr && dialog_size->currentData().toString() == QStringLiteral("normal");
    press_dialog_button(dialog, QDialogButtonBox::Cancel);
  });
  window.show_tool_palette_customization();
  settle();
  CHECK(synced);
  window.apply_tool_palette_preferences(patchy::ui::ToolbarIconSize::Normal, patchy::ui::ToolFlyoutOpenMode::Hold);
  CHECK(!patchy::ui::app_settings().contains(QStringLiteral("preferences/toolFlyoutOpenMode")));
}

// GitHub issue 213 follow-up (and 212): the dialog's bottom-of-palette boxes, toolbar icon size
// and tool group opening change the preview; OK hides the footer rows (the rows under a hidden
// one move up, the tools get the room), stores them, and a new window reads them; the shortcut
// column keeps clear of the list's right edge.
void ui_tool_palette_footer_items_hide_and_preview() {
  CustomizationSettingsGuard settings_guard;
  auto&& [window, canvas] = ShownWindowFixture{};
  auto* palette = tool_palette(window);
  auto* footer = window.findChild<patchy::ui::ToolPaletteFooter*>(QStringLiteral("toolPaletteFooter"));
  CHECK(footer != nullptr);
  if (palette == nullptr || footer == nullptr) {
    return;
  }
  window.resize(window.width(), 1400);
  settle();
  CHECK(footer->footer_height() == patchy::ui::ToolPaletteFooter::kHeight);
  const int full_bottom_margin = palette->layout()->contentsMargins().bottom();

  bool ran = false;
  when_customize_dialog_opens([&](QDialog& dialog) {
    ran = true;
    auto* preview = dialog.findChild<QWidget*>(QStringLiteral("toolPaletteCustomizePreview"));
    auto* tree = dialog.findChild<QTreeWidget*>(QStringLiteral("toolPaletteCustomizeTree"));
    auto* colors = dialog.findChild<QCheckBox*>(QStringLiteral("toolPaletteShowColorsCheck"));
    auto* quick_mask = dialog.findChild<QCheckBox*>(QStringLiteral("toolPaletteShowQuickMaskCheck"));
    auto* screen_mode = dialog.findChild<QCheckBox*>(QStringLiteral("toolPaletteShowScreenModeCheck"));
    auto* icon_size = dialog.findChild<QComboBox*>(QStringLiteral("toolPaletteIconSizeCombo"));
    auto* flyout_open = dialog.findChild<QComboBox*>(QStringLiteral("toolPaletteFlyoutOpenCombo"));
    CHECK(preview != nullptr && tree != nullptr && colors != nullptr && quick_mask != nullptr &&
          screen_mode != nullptr && icon_size != nullptr && flyout_open != nullptr);
    if (preview == nullptr || tree == nullptr || colors == nullptr || quick_mask == nullptr ||
        screen_mode == nullptr || icon_size == nullptr || flyout_open == nullptr) {
      dialog.reject();
      return;
    }
    CHECK(colors->isChecked() && quick_mask->isChecked() && screen_mode->isChecked());
    CHECK(flyout_open->currentData().toString() == QStringLiteral("hold"));
    // The shortcut keys keep a gap before the list's right edge.
    if (auto* hand = tree_row(*tree, QStringLiteral("Hand")); hand != nullptr) {
      CHECK(tree->columnWidth(1) - tree->fontMetrics().horizontalAdvance(hand->text(1)) >= 12);
    }
    settle();
    // The whole preview fits in the dialog, footer rows included.
    CHECK(preview->mapTo(&dialog, QPoint(0, preview->height())).y() <= dialog.height());
    (void)capture_widget_artifact("ui_tool_palette_customize_dialog_options", dialog);
    int height = preview->height();
    for (auto* box : {colors, quick_mask, screen_mode}) {
      box->setChecked(false);
      QApplication::processEvents();
      CHECK(preview->height() < height);
      height = preview->height();
    }
    const int normal_width = preview->width();
    icon_size->setCurrentIndex(icon_size->findData(QStringLiteral("bigger")));
    QApplication::processEvents();
    CHECK(preview->width() > normal_width);
    flyout_open->setCurrentIndex(flyout_open->findData(QStringLiteral("hover")));
    settle();
    CHECK(preview->mapTo(&dialog, QPoint(0, preview->height())).y() <= dialog.height());
    (void)capture_widget_artifact("ui_tool_palette_customize_dialog_options_changed", dialog);
    press_dialog_button(dialog, QDialogButtonBox::Ok);
  });
  window.show_tool_palette_customization();
  CHECK(ran);
  settle();
  const QStringList all_hidden{QStringLiteral("colors"), QStringLiteral("quick_mask"),
                               QStringLiteral("screen_mode")};
  CHECK(window.tool_palette_customization().hidden_footer_items == all_hidden);
  CHECK(patchy::ui::app_settings().value(QStringLiteral("toolPalette/hiddenFooterItems")).toStringList() ==
        all_hidden);
  CHECK(patchy::ui::stored_toolbar_icon_size() == patchy::ui::ToolbarIconSize::Bigger);
  CHECK(window.tool_flyout_open_mode() == patchy::ui::ToolFlyoutOpenMode::Hover);
  CHECK(footer->footer_height() == 0);
  CHECK(!button_named(window, "quickMaskButton")->isVisible());
  CHECK(!button_named(window, "screenModeButton")->isVisible());
  CHECK(!window.findChild<QWidget*>(QStringLiteral("foregroundColorButton"))->isVisible());
  // The tools get the footer's room: the layout's bottom margin no longer reserves it.
  window.apply_tool_palette_preferences(patchy::ui::ToolbarIconSize::Normal, patchy::ui::ToolFlyoutOpenMode::Hold);
  settle();
  CHECK(palette->layout()->contentsMargins().bottom() == full_bottom_margin - patchy::ui::ToolPaletteFooter::kHeight);

  // Only the swatches hidden: Quick Mask moves to the top of the footer, screen mode under it.
  auto customization = window.tool_palette_customization();
  customization.hidden_footer_items = {QStringLiteral("colors")};
  window.apply_tool_palette_customization(customization);
  settle();
  constexpr int kRows =
      patchy::ui::ToolPaletteFooter::kQuickMaskRowHeight + patchy::ui::ToolPaletteFooter::kScreenModeRowHeight;
  CHECK(footer->footer_height() == kRows);
  auto* quick_mask_button = button_named(window, "quickMaskButton");
  auto* screen_mode_button = button_named(window, "screenModeButton");
  CHECK(quick_mask_button->isVisible() && screen_mode_button->isVisible());
  CHECK(quick_mask_button->geometry().top() < patchy::ui::ToolPaletteFooter::kQuickMaskRowHeight);
  CHECK(screen_mode_button->geometry().top() >= quick_mask_button->geometry().bottom());
  CHECK(palette->layout()->contentsMargins().bottom() ==
        full_bottom_margin - patchy::ui::ToolPaletteFooter::kHeight + kRows);
  (void)capture_widget_artifact("ui_tool_palette_without_color_swatches", *palette);

  // A new window reads the stored choice.
  {
    patchy::ui::MainWindow second;
    CHECK(second.tool_palette_customization().hidden_footer_items == QStringList{QStringLiteral("colors")});
    auto* second_footer = second.findChild<patchy::ui::ToolPaletteFooter*>(QStringLiteral("toolPaletteFooter"));
    CHECK(second_footer != nullptr && second_footer->footer_height() == kRows);
  }

  // Back to the default: every row, no key.
  window.apply_tool_palette_customization(ToolPaletteCustomization{});
  settle();
  CHECK(footer->footer_height() == patchy::ui::ToolPaletteFooter::kHeight);
  CHECK(palette->layout()->contentsMargins().bottom() == full_bottom_margin);
  CHECK(window.findChild<QWidget*>(QStringLiteral("foregroundColorButton"))->isVisible());
  CHECK(!patchy::ui::app_settings().contains(QStringLiteral("toolPalette/hiddenFooterItems")));
}

// GitHub issue 459: when a tool group opens. Click opens it on the press and selects the tool
// the button shows; Hover opens it after the pointer rests on the button, selects nothing, and
// closes it once the pointer leaves the button and the menu; Click and hold stays the default.
// The choice is stored under preferences/toolFlyoutOpenMode and a new window reads it.
void ui_tool_flyout_open_modes_click_and_hover() {
  CustomizationSettingsGuard settings_guard;
  auto&& [window, canvas] = ShownWindowFixture{};
  window.activateWindow();
  settle();
  CHECK(window.isActiveWindow());
  auto* marquee_button = button_named(window, "marqueeToolButton");
  auto* lasso_button = button_named(window, "lassoToolButton");
  auto* brush = require_action(window, "toolBrushAction");
  auto* marquee = require_action(window, "toolMarqueeAction");
  CHECK(window.tool_flyout_open_mode() == patchy::ui::ToolFlyoutOpenMode::Hold);
  CHECK(marquee_button->popupMode() == QToolButton::DelayedPopup);

  // Click: the press opens the flyout at once and selects the tool the button shows.
  window.apply_tool_palette_preferences(patchy::ui::ToolbarIconSize::Normal, patchy::ui::ToolFlyoutOpenMode::Click);
  CHECK(patchy::ui::app_settings().value(QStringLiteral("preferences/toolFlyoutOpenMode")).toString() ==
        QStringLiteral("click"));
  CHECK(marquee_button->popupMode() == QToolButton::InstantPopup);
  brush->trigger();
  settle();
  {
    auto* menu = marquee_button->menu();
    int shows = 0;
    bool shown = false;
    const auto connection = QObject::connect(menu, &QMenu::aboutToShow, menu, [&] {
      ++shows;
      QTimer::singleShot(0, menu, [&] {
        shown = menu->isVisible();
        menu->close();
      });
    });
    const QPoint center = marquee_button->rect().center();
    send_mouse(*marquee_button, QEvent::MouseButtonPress, center, Qt::LeftButton, Qt::LeftButton);
    process_events_until([&] { return shows > 0; }, 1000);
    send_mouse(*marquee_button, QEvent::MouseButtonRelease, center, Qt::LeftButton, Qt::NoButton);
    settle();
    CHECK(shows == 1);
    CHECK(shown);
    CHECK(marquee->isChecked());
    QObject::disconnect(connection);
  }

  // Hover: resting on the Lasso group opens its flyout, the Brush stays active, and leaving
  // the button and the menu closes it.
  window.apply_tool_palette_preferences(patchy::ui::ToolbarIconSize::Normal, patchy::ui::ToolFlyoutOpenMode::Hover);
  CHECK(lasso_button->popupMode() == QToolButton::DelayedPopup);
  brush->trigger();
  settle();
  {
    auto* menu = lasso_button->menu();
    int shows = 0;
    bool shown_beside = false;
    bool tool_kept = false;
    bool closed_on_leave = false;
    const auto connection = QObject::connect(menu, &QMenu::aboutToShow, menu, [&] {
      ++shows;
      QTimer::singleShot(0, menu, [&] {
        process_events_until([&] { return menu->isVisible(); }, 1000);
        shown_beside = menu->isVisible() &&
                       menu->geometry().left() > lasso_button->mapToGlobal(QPoint(lasso_button->width(), 0)).x();
        tool_kept = brush->isChecked();
        // Across the gap onto the menu keeps it open.
        QCursor::setPos(menu->geometry().center());
        process_events_for(500);
        const bool stayed = menu->isVisible();
        // Off both: the menu closes by itself.
        QCursor::setPos(window.mapToGlobal(QPoint(window.width() / 2, window.height() / 2)));
        process_events_until([&] { return !menu->isVisible(); }, 2000);
        closed_on_leave = stayed && !menu->isVisible();
        if (menu->isVisible()) {
          menu->close();
        }
      });
    });
    QCursor::setPos(lasso_button->mapToGlobal(lasso_button->rect().center()));
    process_events_for(100);
    CHECK(shows == 0);
    process_events_until([&] { return shows > 0 && !menu->isVisible(); }, 4000);
    settle();
    CHECK(shows == 1);
    CHECK(shown_beside);
    CHECK(tool_kept);
    CHECK(closed_on_leave);
    CHECK(brush->isChecked());
    QObject::disconnect(connection);
  }
  CHECK(patchy::ui::stored_tool_flyout_open_mode() == patchy::ui::ToolFlyoutOpenMode::Hover);
  {
    patchy::ui::MainWindow second;
    CHECK(second.tool_flyout_open_mode() == patchy::ui::ToolFlyoutOpenMode::Hover);
  }

  // Back to Click and hold, stored as no key.
  window.apply_tool_palette_preferences(patchy::ui::ToolbarIconSize::Normal, patchy::ui::ToolFlyoutOpenMode::Hold);
  CHECK(marquee_button->popupMode() == QToolButton::DelayedPopup);
  CHECK(!patchy::ui::app_settings().contains(QStringLiteral("preferences/toolFlyoutOpenMode")));
  QCursor::setPos(window.mapToGlobal(QPoint(window.width() / 2, window.height() / 2)));
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
  CHECK(backlog_contains(window, QStringLiteral("layout=oneColumn wheel=true gear=false hidden=0")));
  // showGearButton is a no-op now (the palette has no gear): accepted, still false.
  CHECK(run_script(window, QStringLiteral("patchy.ui.toolPalette = {showGearButton: true};"
                                          "console.log('gear now=' + patchy.ui.toolPalette.showGearButton);")));
  CHECK(backlog_contains(window, QStringLiteral("gear now=false")));
  CHECK(window.tool_palette_customization() == ToolPaletteCustomization{});
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
      {"ui_tool_palette_settings_from_an_older_version_show_new_tools",
       ui_tool_palette_settings_from_an_older_version_show_new_tools},
      {"ui_tool_palette_dialog_lists_every_palette_tool", ui_tool_palette_dialog_lists_every_palette_tool},
      {"ui_preferences_opens_customize_tool_palette", ui_preferences_opens_customize_tool_palette},
      {"ui_tool_palette_footer_items_hide_and_preview", ui_tool_palette_footer_items_hide_and_preview},
      {"ui_tool_flyout_open_modes_click_and_hover", ui_tool_flyout_open_modes_click_and_hover},
  };
}
