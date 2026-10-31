// Customize Tool Palette (GitHub issues 213, 316 and 199; docs/tool-palette.md, "Customizing
// the palette"): hidden tools keep their keys, flyout members leave their menu and Shift+letter
// cycle, the two-column layout, wheel scrolling in a short window, the Layers footer's
// show/hide menu, the dialog's OK, Cancel and Reset to Default, and the stored settings. Also
// the palette's bottom items, the toolbar icon size and tool group opening shown in the dialog
// (213 follow-up), and when tool groups open: click and hold, click or hover (459). Groups with
// one tool, no tool groups, clicks handed on from an open flyout, the two-column footer and
// the shortcut key column (499), and the resizable dialog's preview that shows the rows that fit (499 comment).
#include "core/layer_metadata.hpp"
#include "ui/action_icons.hpp"
#include "ui/app_settings.hpp"
#include "ui/app_style.hpp"
#include "ui/canvas_widget.hpp"
#include "ui/layer_thumbnail_size.hpp"
#include "ui/localization.hpp"
#include "ui/main_window.hpp"
#include "ui/tool_palette_customization.hpp"
#include "ui/tool_palette_footer.hpp"
#include "ui/tool_palette_layout.hpp"
#include "ui/theme_palette.hpp"
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
#include <QPainter>
#include <QHeaderView>
#include <QImage>
#include <QPushButton>
#include <QRadioButton>
#include <QScreen>
#include <QScrollBar>
#include <QStyleFactory>
#include <QStyleOptionViewItem>
#include <QStyledItemDelegate>
#include <QTabWidget>
#include <QTest>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <array>
#include <functional>
#include <iostream>
#include <initializer_list>
#include <iterator>
#include <map>
#include <memory>
#include <string>
#include <utility>
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
                            "layersPanel/shownFooterButtons", "toolPalette/hiddenFooterItems",
                            "layersPanel/footerButtonOrder", "preferences/toolbarIconSize",
                            "preferences/toolFlyoutOpenMode", "layersPanel/footerIconSize",
                            "toolPalette/ungroupTools", "toolPalette/customizeDialogSize"}) {
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

// Logical pixels between the rightmost inked pixel of a row's shortcut column and the right
// edge of the list's viewport; -1 when the row draws nothing there.
int shortcut_gap_before_right_edge(QTreeWidget& tree, QTreeWidgetItem& row) {
  auto* viewport = tree.viewport();
  const QRect band = tree.visualItemRect(&row);
  const QImage image = viewport->grab().toImage();
  const double scale = static_cast<double>(image.width()) / viewport->width();
  const int left = qRound(tree.header()->sectionViewportPosition(1) * scale);
  const int top = qRound(band.top() * scale);
  const int bottom = qRound((band.bottom() + 1) * scale) - 1;
  const QColor background = image.pixelColor(image.width() - 1, (top + bottom) / 2);
  int rightmost = -1;
  for (int y = std::max(0, top); y <= std::min(bottom, image.height() - 1); ++y) {
    for (int x = std::max(0, left); x < image.width(); ++x) {
      const QColor color = image.pixelColor(x, y);
      if (std::abs(color.red() - background.red()) + std::abs(color.green() - background.green()) +
              std::abs(color.blue() - background.blue()) >
          90) {
        rightmost = std::max(rightmost, x);
      }
    }
  }
  if (rightmost < 0) {
    return -1;
  }
  return static_cast<int>((image.width() - 1 - rightmost) / scale);
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
  // The arrow covers Qt's extension band; the palette's own expand row sits under it
  // (GitHub issue 488, ui_tool_palette_scroll_mode_keeps_expand_button_reachable).
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

// GitHub issue 488: in wheel-scroll mode the expand arrow that opens the extra columns has a
// full-width row of its own directly above the footer, under the scroll-down arrow. It stays
// there once the column is scrolled to its last tool (nothing overflows below then, so Qt
// hides its own extension), and a click scrolls back to the top and opens every tool in
// columns. Both layouts.
void ui_tool_palette_scroll_mode_keeps_expand_button_reachable() {
  CustomizationSettingsGuard settings_guard;
  auto&& [window, canvas] = ShownWindowFixture{};
  window.setAnimated(false);
  auto* palette = tool_palette(window);
  auto* layout = window.tool_palette_layout();
  CHECK(layout != nullptr);
  if (palette == nullptr || layout == nullptr) {
    return;
  }
  auto* down = palette->findChild<QWidget*>(QStringLiteral("toolPaletteScrollDownArrow"));
  auto* extension = palette->findChild<QToolButton*>(QStringLiteral("qt_toolbar_ext_button"));
  auto* footer = palette->findChild<QWidget*>(QStringLiteral("toolPaletteFooter"));
  auto* zoom = button_named(window, "zoomToolButton");
  auto* move = button_named(window, "moveToolButton");
  CHECK(down != nullptr && extension != nullptr && footer != nullptr);
  if (down == nullptr || extension == nullptr || footer == nullptr || zoom == nullptr || move == nullptr) {
    return;
  }
  const int tool_width = move->width();
  // The expand row: visible, whole inside the bar, directly above the footer, as wide as a
  // tool button and clear of every tool and the scroll arrow.
  const auto check_expand_row = [&](const char* stage, bool expect_down) -> QWidget* {
    auto* expand = palette->findChild<QWidget*>(QStringLiteral("toolPaletteExpandArrow"));
    CHECK(expand != nullptr);
    if (expand == nullptr) {
      std::printf("  %s: no expand arrow\n", stage);
      return nullptr;
    }
    const auto row = expand->geometry();
    const auto footer_rect = footer->geometry();
    if (!expand->isVisible() || row.bottom() >= footer_rect.top() || row.bottom() < footer_rect.top() - 2 ||
        row.width() < tool_width || row.height() < 14) {
      std::printf("  %s: expand %d,%d %dx%d visible %d, footer top %d\n", stage, row.x(), row.y(), row.width(),
                  row.height(), expand->isVisible() ? 1 : 0, footer_rect.top());
    }
    CHECK(expand->isVisible());
    CHECK(row.bottom() < footer_rect.top());
    CHECK(row.bottom() >= footer_rect.top() - 2);
    CHECK(row.width() >= tool_width);
    CHECK(row.height() >= 14);
    CHECK(palette->rect().contains(row));
    if (down->isVisible() != expect_down) {
      std::printf("  %s: down arrow visible %d, extension visible %d at %d, offset %d, can scroll down %d\n", stage,
                  down->isVisible() ? 1 : 0, extension->isVisible() ? 1 : 0, extension->y(), layout->scroll_offset(),
                  layout->can_scroll_down() ? 1 : 0);
    }
    CHECK(down->isVisible() == expect_down);
    if (down->isVisible()) {
      CHECK(down->geometry().bottom() < row.top());
      CHECK(down->height() >= 14);
      CHECK(down->width() >= tool_width);
    }
    for (auto* button : palette->findChildren<QToolButton*>()) {
      if (button == extension || footer->isAncestorOf(button) || !button->isVisible()) {
        continue;
      }
      CHECK(!geometry_in_palette(*palette, *button).intersects(row));
    }
    return expand;
  };
  const auto click = [](QWidget& target) {
    QTest::mouseClick(&target, Qt::LeftButton, Qt::NoModifier, target.rect().center());
    settle();
  };
  // capture_widget_artifact re-themes the window with the Normal sheet while it grabs, which
  // re-lays a scaled palette; the Biggest palette is grabbed as it stands.
  const auto save_palette = [&](const char* name, bool scaled) {
    if (scaled) {
      (void)palette->grab().save(QStringLiteral("test-artifacts/%1.png").arg(QLatin1String(name)));
    } else {
      (void)capture_widget_artifact(name, *palette);
    }
    settle();
  };

  for (const bool two_columns : {false, true}) {
    const char* label = two_columns ? "two columns" : "one column";
    std::printf("  %s\n", label);
    auto customization = window.tool_palette_customization();
    customization.two_columns = two_columns;
    window.apply_tool_palette_customization(customization);
    if (two_columns) {
      // Two columns are short enough to fit the window's minimum height at Normal; the
      // biggest icons also cover a scaled palette.
      window.apply_tool_palette_preferences(patchy::ui::ToolbarIconSize::Biggest,
                                            patchy::ui::ToolFlyoutOpenMode::Hold);
    }
    shrink_palette(window, *palette, 150);
    if (!layout->can_scroll_down()) {
      std::printf("  no overflow: window %d, palette %d, hint %d\n", window.height(), palette->height(),
                  palette->sizeHint().height());
    }
    CHECK(layout->can_scroll_down());
    save_palette(two_columns ? "ui_tool_palette_488_short_two_columns" : "ui_tool_palette_488_short", two_columns);
    check_expand_row("short", true);

    // Scrolled to the last tool: nothing overflows below, the expand arrow stays put.
    for (int notches = 0; notches < 30 && layout->can_scroll_down(); ++notches) {
      send_wheel(*palette, -120);
    }
    CHECK(zoom->isVisible());
    CHECK(!move->isVisible());
    CHECK(!layout->can_scroll_down());
    save_palette(two_columns ? "ui_tool_palette_488_scrolled_two_columns" : "ui_tool_palette_488_scrolled",
                 two_columns);
    auto* expand = check_expand_row("scrolled to the bottom", false);
    if (expand == nullptr) {
      continue;
    }

    // A click opens the columns with every tool, the first one included.
    const int collapsed_width = palette->width();
    click(*expand);
    CHECK(process_events_until([&] { return palette->width() > collapsed_width && extension->isChecked(); }));
    CHECK(layout->scroll_offset() == 0);
    CHECK(move->isVisible());
    CHECK(zoom->isVisible());
    CHECK(!expand->isVisible());
    CHECK(!down->isVisible());
    CHECK(extension->isVisible());
    CHECK(extension->geometry().bottom() < footer->geometry().top());
    CHECK(extension->geometry().bottom() >= footer->geometry().top() - 2);
    save_palette(two_columns ? "ui_tool_palette_488_expanded_two_columns" : "ui_tool_palette_488_expanded",
                 two_columns);
    // The wheel scrolls nothing while the columns are open.
    send_wheel(*palette, -120);
    CHECK(layout->scroll_offset() == 0);

    // The < arrow closes them; the column overflows again with both rows back.
    click(*extension);
    CHECK(process_events_until([&] { return palette->width() <= collapsed_width && !extension->isChecked(); }));
    settle();
    check_expand_row("collapsed again", true);
  }

  // Painted from theme roles: the chevron shows on the bar in both schemes.
  auto* expand = palette->findChild<QWidget*>(QStringLiteral("toolPaletteExpandArrow"));
  if (expand != nullptr) {
    const auto saved_scheme = patchy::ui::active_color_scheme();
    for (const auto scheme : {patchy::ui::ColorScheme::Dark, patchy::ui::ColorScheme::Light}) {
      patchy::ui::set_active_color_scheme(scheme);
      const auto image = expand->grab().toImage();
      const auto background = patchy::ui::theme().tool_palette_bg;
      int inked = 0;
      for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
          const QColor pixel = image.pixelColor(x, y);
          if (std::abs(pixel.red() - background.red()) + std::abs(pixel.green() - background.green()) +
                  std::abs(pixel.blue() - background.blue()) > 120) {
            ++inked;
          }
        }
      }
      CHECK(inked >= 6);
    }
    patchy::ui::set_active_color_scheme(saved_scheme);
  }
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
  CHECK(menu->actions().size() == static_cast<qsizetype>(std::size(patchy::ui::kLayerFooterButtonIds)) + 3);
  (void)capture_widget_artifact("ui_layer_footer_buttons_menu", *menu);
  if (toggle != nullptr) {
    toggle->trigger();
  }
  menu->close();
  settle();
  CHECK(!animation->isVisible());
  // With the optional buttons (GitHub issue 470), still hidden by default.
  const QStringList hidden_with_animation{QStringLiteral("animation"), QStringLiteral("merge_down"),
                                          QStringLiteral("merge_visible"), QStringLiteral("move_down"),
                                          QStringLiteral("move_up"), QStringLiteral("rasterize")};
  CHECK(window.tool_palette_customization().hidden_layer_buttons == hidden_with_animation);
  CHECK(patchy::ui::stored_tool_palette_customization().hidden_layer_buttons == hidden_with_animation);
  CHECK(patchy::ui::app_settings().value(QStringLiteral("layersPanel/hiddenFooterButtons")).toStringList() ==
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

// GitHub issue 511: the Layers panel settings gear at the right end of the panel's own title
// row (below the tab bar, like the Color panel's) pops up Customize Layers Panel... (Customize
// Tool Palette on its Layers Panel tab; GitHub issue 538 named both), then the Thumbnail Size and Thumbnail Display
// submenus, whose actions are the thumbnail menus' own. It stays in the Layers title row when
// the panel is collapsed, leaves its tab group or comes back to it.
void ui_layers_panel_gear_opens_layer_button_customize() {
  CustomizationSettingsGuard settings_guard;
  SettingsValueRestorer restore_size{QString::fromLatin1(patchy::ui::kLayerThumbnailSizeSettingsKey)};
  patchy::ui::app_settings().remove(QLatin1String(patchy::ui::kLayerThumbnailSizeSettingsKey));
  auto&& [window, canvas] = ShownWindowFixture{};
  auto* column = patchy::ui::MainWindowTestAccess::panel_column(window);
  auto* gear = window.findChild<QToolButton*>(QStringLiteral("layersPanelSettingsButton"));
  auto* title = window.findChild<QWidget*>(QStringLiteral("layersDockTitle"));
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(column != nullptr && gear != nullptr && title != nullptr && layer_list != nullptr);
  if (column == nullptr || gear == nullptr || title == nullptr || layer_list == nullptr) {
    return;
  }
  const auto layers = QStringLiteral("layersDock");
  const auto channels = QStringLiteral("channelsDock");
  const auto paths = QStringLiteral("pathsDock");
  const auto check_in_title_row = [&](const char* context) {
    std::cout << "  gear: " << context << '\n';
    CHECK(gear->parentWidget() == title);
    CHECK(gear->isVisible() && gear->isEnabled());
    CHECK(gear->geometry().right() >= title->width() - 12);
    CHECK(title->rect().contains(gear->geometry()));
  };
  CHECK(column->in_tab_group(layers));
  check_in_title_row("in the Layers tab group");
  CHECK(gear->toolTip() == QStringLiteral("Layers panel settings"));
  CHECK(gear->accessibleName() == gear->toolTip());
  CHECK(gear->popupMode() == QToolButton::InstantPopup);
  // The Color panel's gear's size, from the same window sheet rule.
  auto* color_gear = window.findChild<QToolButton*>(QStringLiteral("colorPanelSettingsButton"));
  CHECK(color_gear != nullptr);
  if (color_gear != nullptr) {
    color_gear->ensurePolished();
    CHECK(gear->size() == color_gear->size());
  }
  // Every title row keeps one height.
  auto* palette_title = window.findChild<QWidget*>(QStringLiteral("paletteDockTitle"));
  CHECK(palette_title != nullptr && title->sizeHint().height() == palette_title->sizeHint().height());

  // The menu: Customize Layers Panel..., a separator, Thumbnail Size, Thumbnail Display.
  auto* menu = gear->menu();
  CHECK(menu != nullptr && menu->objectName() == QStringLiteral("layersPanelSettingsMenu"));
  if (menu == nullptr) {
    return;
  }
  const auto entries = menu->actions();
  CHECK(entries.size() == 4);
  if (entries.size() != 4) {
    return;
  }
  auto* customize = entries[0];
  CHECK(customize->objectName() == QStringLiteral("layersPanelCustomizeButtonsAction"));
  CHECK(customize->text() == QStringLiteral("Customize Layers Panel..."));
  CHECK(entries[1]->isSeparator());
  auto* size_menu = entries[2]->menu();
  auto* display_menu = entries[3]->menu();
  CHECK(size_menu != nullptr && size_menu->objectName() == QStringLiteral("layerThumbnailSizeMenu"));
  CHECK(display_menu != nullptr && display_menu->objectName() == QStringLiteral("layerThumbnailDisplayMenu"));
  if (size_menu == nullptr || display_menu == nullptr) {
    return;
  }
  CHECK(size_menu->title() == QStringLiteral("Thumbnail Size"));
  CHECK(display_menu->title() == QStringLiteral("Thumbnail Display"));
  const auto checked_in = [](QMenu& submenu) {
    QString name;
    for (auto* action : submenu.actions()) {
      if (action->isChecked()) {
        name = action->objectName();
      }
    }
    return name;
  };
  CHECK(size_menu->actions().size() == 4);
  CHECK(display_menu->actions().size() == 2);
  CHECK(checked_in(*size_menu) == QStringLiteral("layerThumbnailSizeMediumAction"));
  // The shared actions, not copies.
  auto* small = window.findChild<QAction*>(QStringLiteral("layerThumbnailSizeSmallAction"));
  auto* whole_document = window.findChild<QAction*>(QStringLiteral("layerThumbnailBoundsDocumentAction"));
  CHECK(small != nullptr && size_menu->actions().contains(small));
  CHECK(whole_document != nullptr && display_menu->actions().contains(whole_document));
  if (small == nullptr) {
    return;
  }

  // The gear's menu open with Thumbnail Size beside it, painted over the Layers section.
  bool shown = false;
  QTimer::singleShot(0, [&] {
    auto* open = qobject_cast<QMenu*>(QApplication::activePopupWidget());
    CHECK(open == menu);
    if (open != menu) {
      return;
    }
    shown = true;
    auto* section = column->section_widget(layers);
    // Placed for the picture as at the right edge of a screen: the menu right-aligned under
    // the gear and the submenu opening to its left (the offscreen screen places them anywhere).
    const auto gear_corner = gear->mapToGlobal(QPoint(gear->width(), gear->height()));
    menu->move(gear_corner - QPoint(menu->width(), 0));
    QApplication::processEvents();
    const QRect size_row = menu->actionGeometry(entries[2]);
    size_menu->popup(menu->mapToGlobal(QPoint(0, size_row.top())));
    QApplication::processEvents();
    size_menu->move(menu->mapToGlobal(QPoint(-size_menu->width() + 2, size_row.top() - 4)));
    QApplication::processEvents();
    if (section != nullptr) {
      const auto origin = section->mapToGlobal(QPoint(0, 0));
      const QRect menu_rect(menu->mapToGlobal(QPoint(0, 0)) - origin, menu->size());
      const QRect size_rect(size_menu->mapToGlobal(QPoint(0, 0)) - origin, size_menu->size());
      const QRect all = QRect(QPoint(0, 0), section->size()).united(menu_rect).united(size_rect);
      QImage shot(all.size(), QImage::Format_ARGB32_Premultiplied);
      shot.fill(Qt::transparent);
      {
        QPainter painter(&shot);
        painter.drawPixmap(-all.topLeft(), section->grab());
        painter.drawPixmap(menu_rect.topLeft() - all.topLeft(), menu->grab());
        painter.drawPixmap(size_rect.topLeft() - all.topLeft(), size_menu->grab());
      }
      for (QWidget* captured : {static_cast<QWidget*>(section), static_cast<QWidget*>(menu),
                                static_cast<QWidget*>(size_menu)}) {
        CHECK(capture_theme_mismatch(*captured).isEmpty());
      }
      save_image_artifact("ui_layers_panel_gear_menu", shot);
    }
    size_menu->close();
    menu->close();
  });
  gear->showMenu();
  settle();
  CHECK(shown);

  // Small from the gear's menu resizes the rows, is stored, and shows ticked in the pixel
  // thumbnail's own right-click menu.
  small->trigger();
  settle();
  CHECK(require_layer_item(*layer_list, QStringLiteral("Paint Layer"))->sizeHint().height() == 36);
  CHECK(patchy::ui::app_settings().value(QLatin1String(patchy::ui::kLayerThumbnailSizeSettingsKey)).toString() ==
        QStringLiteral("small"));
  CHECK(checked_in(*size_menu) == QStringLiteral("layerThumbnailSizeSmallAction"));
  QString thumbnail_menu_size;
  QString thumbnail_menu_name;
  QTimer::singleShot(0, [&] {
    auto* open = qobject_cast<QMenu*>(QApplication::activePopupWidget());
    CHECK(open != nullptr);
    if (open == nullptr) {
      return;
    }
    thumbnail_menu_name = open->objectName();
    if (auto* thumbnail_size = open->findChild<QMenu*>(QStringLiteral("layerThumbnailSizeMenu"));
        thumbnail_size != nullptr) {
      thumbnail_menu_size = checked_in(*thumbnail_size);
    }
    open->close();
  });
  auto* row = layer_list->itemWidget(require_layer_item(*layer_list, QStringLiteral("Paint Layer")));
  auto* thumbnail = row != nullptr ? row->findChild<QWidget*>(QStringLiteral("layerContentThumbnail")) : nullptr;
  CHECK(thumbnail != nullptr);
  if (thumbnail != nullptr) {
    const auto local = thumbnail->rect().center();
    QContextMenuEvent event(QContextMenuEvent::Mouse, local, thumbnail->mapToGlobal(local));
    QApplication::sendEvent(thumbnail, &event);
  }
  settle();
  CHECK(thumbnail_menu_name == QStringLiteral("layerThumbnailMenu"));
  CHECK(thumbnail_menu_size == QStringLiteral("layerThumbnailSizeSmallAction"));

  // Customize Layers Panel... opens the dialog on the Layers Panel tab; Cancel changes
  // nothing. It opens once the menu has closed, so the watcher is queued after it.
  const auto before = window.tool_palette_customization();
  bool ran = false;
  customize->trigger();
  when_customize_dialog_opens([&](QDialog& dialog) {
    ran = true;
    auto* tabs = dialog.findChild<QTabWidget*>(QStringLiteral("toolPaletteCustomizeTabs"));
    CHECK(tabs != nullptr && tabs->currentIndex() == 1);
    CHECK(tabs == nullptr || tabs->tabText(1) == QStringLiteral("Layers Panel"));
    CHECK(dialog.findChild<QListWidget*>(QStringLiteral("layerFooterButtonsList")) != nullptr);
    dialog.reject();
  });
  process_events_until([&] { return ran; });
  CHECK(ran);
  settle();
  CHECK(window.tool_palette_customization().hidden_layer_buttons == before.hidden_layer_buttons);

  // Collapsed, the title row and its gear stay.
  column->set_panel_expanded(layers, false);
  settle();
  check_in_title_row("collapsed");
  column->set_panel_expanded(layers, true);
  settle();
  // Out of its tab group and back in.
  column->separate_panel(layers, 0);
  settle();
  CHECK(!column->in_tab_group(layers));
  check_in_title_row("in a section of its own");
  CHECK(gear->menu() == menu);
  column->set_tab_groups({{layers, channels, paths}});
  column->bring_to_front(layers);
  settle();
  CHECK(column->in_tab_group(layers));
  check_in_title_row("back in its tab group");
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
    // The default column runs past the preview's 10 rows, so a hidden tool shows in the row count.
    const int preview_rows = preview->property("toolRows").toInt();
    if (auto* hand = tree_row(*tree, QStringLiteral("Hand")); hand != nullptr) {
      CHECK(hand->checkState(0) == Qt::Checked);
      hand->setCheckState(0, Qt::Unchecked);
    }
    QApplication::processEvents();
    CHECK(preview->property("toolRows").toInt() == preview_rows - 1);
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
    CHECK(layer_list != nullptr &&
          layer_list->count() == static_cast<int>(std::size(patchy::ui::kLayerFooterButtonIds)));
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
                            "toolPalette/showCustomizeButton", "layersPanel/hiddenFooterButtons",
                            "layersPanel/shownFooterButtons", "layersPanel/footerButtonOrder"}) {
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
  auto* ruler_button = button_named(window, "rulerMemberButton");
  auto* ruler = require_action(window, "toolRulerAction");
  auto* pick = require_action(window, "toolPickAction");
  // The group has one tool left, so its plain button stands in for it (GitHub issue 499).
  CHECK(!on_palette(*palette, pick_button));
  CHECK(on_palette(*palette, ruler_button));
  CHECK(ruler_button->defaultAction() == ruler);
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
      if (slot.button != button_named(window, "handToolButton") && slot.button != pick_button) {
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
    settle();
    // The shortcut letters keep a clear gap before the list's right edge, where the scrollbar
    // sits (GitHub issue 213 follow-up): measured on the drawn pixels, since a padding that
    // only widens the column leaves right-aligned text flush with the edge.
    CHECK(tree->verticalScrollBar()->isVisible());
    for (const auto* name : {"Move Tools", "Brush"}) {
      if (auto* row = tree_row(*tree, QLatin1String(name)); row != nullptr) {
        const int gap = shortcut_gap_before_right_edge(*tree, *row);
        if (gap < 12) {
          std::printf("[INFO] %s: shortcut ends %d px before the list's right edge\n", name, gap);
        }
        CHECK(gap >= 12);
      }
    }
    // The whole preview fits in the dialog, footer rows included.
    CHECK(preview->mapTo(&dialog, QPoint(0, preview->height())).y() <= dialog.height());
    (void)capture_widget_artifact("ui_tool_palette_customize_dialog_options", dialog);
    // A footer row left out shortens the strip, or makes room for another row of tools.
    int strip = preview->property("stripHeight").toInt();
    int shown = preview->property("shownToolRows").toInt();
    for (auto* box : {colors, quick_mask, screen_mode}) {
      box->setChecked(false);
      QApplication::processEvents();
      const int new_strip = preview->property("stripHeight").toInt();
      const int new_shown = preview->property("shownToolRows").toInt();
      CHECK(new_strip < strip || new_shown > shown);
      strip = new_strip;
      shown = new_shown;
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
  // No tool groups (GitHub issue 499).
  CHECK(run_script(window, QStringLiteral("console.log('ungroup before=' + patchy.ui.toolPalette.ungroupTools);"
                                          "patchy.ui.toolPalette = {ungroupTools: true};"
                                          "console.log('ungroup after=' + patchy.ui.toolPalette.ungroupTools);")));
  CHECK(backlog_contains(window, QStringLiteral("ungroup before=false")));
  CHECK(backlog_contains(window, QStringLiteral("ungroup after=true")));
  expected.ungroup_tools = true;
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

// GitHub issue 470: Move Layer Up, Move Layer Down and Merge Down join the footer hidden, so it
// keeps its one row; settings from before them (which name only hidden buttons) keep them
// hidden, a shown one is stored under layersPanel/shownFooterButtons, and the default stores
// no key at all.
void ui_layer_footer_optional_buttons_start_hidden() {
  CustomizationSettingsGuard settings_guard;
  const char* const optional_names[] = {"layerMoveUpButton", "layerMoveDownButton", "layerMergeDownButton",
                                        "layerMergeVisibleButton", "layerRasterizeButton"};
  {
    auto&& [window, canvas] = ShownWindowFixture{};
    settle();
    CHECK(window.tool_palette_customization() == ToolPaletteCustomization{});
    for (const auto* name : optional_names) {
      auto* button = window.findChild<QAbstractButton*>(QLatin1String(name));
      CHECK(button != nullptr && !button->isVisible());
    }
    // The eight default buttons share one row.
    auto* delete_button = window.findChild<QPushButton*>(QStringLiteral("layerDeleteButton"));
    auto* new_button = window.findChild<QPushButton*>(QStringLiteral("layerNewButton"));
    CHECK(delete_button != nullptr && new_button != nullptr && delete_button->isVisible() &&
          delete_button->y() == new_button->y());
    auto settings = patchy::ui::app_settings();
    CHECK(!settings.contains(QStringLiteral("layersPanel/hiddenFooterButtons")));
    CHECK(!settings.contains(QStringLiteral("layersPanel/shownFooterButtons")));
  }
  // Settings from an earlier version: Animation hidden, the new buttons not named.
  patchy::ui::app_settings().setValue(QStringLiteral("layersPanel/hiddenFooterButtons"),
                                      QStringList{QStringLiteral("animation")});
  auto&& [window, canvas] = ShownWindowFixture{};
  settle();
  auto* move_up = window.findChild<QPushButton*>(QStringLiteral("layerMoveUpButton"));
  auto* move_down = window.findChild<QPushButton*>(QStringLiteral("layerMoveDownButton"));
  auto* merge_down = window.findChild<QPushButton*>(QStringLiteral("layerMergeDownButton"));
  auto* duplicate = window.findChild<QPushButton*>(QStringLiteral("layerDuplicateButton"));
  auto* animation = window.findChild<QPushButton*>(QStringLiteral("layerAnimationButton"));
  CHECK(move_up != nullptr && move_down != nullptr && merge_down != nullptr && duplicate != nullptr &&
        animation != nullptr);
  if (move_up == nullptr || move_down == nullptr || merge_down == nullptr || duplicate == nullptr ||
      animation == nullptr) {
    return;
  }
  CHECK(!move_up->isVisible() && !move_down->isVisible() && !merge_down->isVisible() && !animation->isVisible());
  // The footer menu shows Move Layer Up; only that is stored, under its own key.
  const QPoint position = duplicate->rect().center();
  QContextMenuEvent event(QContextMenuEvent::Mouse, position, duplicate->mapToGlobal(position));
  QApplication::sendEvent(duplicate, &event);
  QApplication::processEvents();
  auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
  CHECK(menu != nullptr && menu->objectName() == QStringLiteral("layerFooterButtonsMenu"));
  if (menu != nullptr) {
    auto* toggle = menu->findChild<QAction*>(QStringLiteral("layerFooterToggle_move_up"));
    CHECK(toggle != nullptr && !toggle->isChecked());
    if (toggle != nullptr) {
      toggle->trigger();
    }
    menu->close();
  }
  settle();
  CHECK(move_up->isVisible() && !move_down->isVisible() && !animation->isVisible());
  {
    auto settings = patchy::ui::app_settings();
    CHECK(settings.value(QStringLiteral("layersPanel/shownFooterButtons")).toStringList() ==
          QStringList{QStringLiteral("move_up")});
    CHECK(settings.value(QStringLiteral("layersPanel/hiddenFooterButtons")).toStringList() ==
          QStringList{QStringLiteral("animation")});
  }
  // A new window starts with it shown.
  {
    patchy::ui::MainWindow second;
    CHECK(!second.tool_palette_customization().hidden_layer_buttons.contains(QStringLiteral("move_up")));
    CHECK(second.tool_palette_customization().hidden_layer_buttons.contains(QStringLiteral("move_down")));
  }
  // All shown: the footer wraps rather than widening the column.
  auto customization = window.tool_palette_customization();
  customization.hidden_layer_buttons.clear();
  window.apply_tool_palette_customization(customization);
  settle();
  CHECK(move_up->isVisible() && move_down->isVisible() && merge_down->isVisible() && animation->isVisible());
  CHECK(!patchy::ui::app_settings().contains(QStringLiteral("layersPanel/hiddenFooterButtons")));
  CHECK(patchy::ui::app_settings().value(QStringLiteral("layersPanel/shownFooterButtons")).toStringList() ==
        (QStringList{QStringLiteral("merge_down"), QStringLiteral("merge_visible"), QStringLiteral("move_down"),
                     QStringLiteral("move_up"), QStringLiteral("rasterize")}));
  if (auto* layers_panel = duplicate->parentWidget(); layers_panel != nullptr) {
    (void)capture_widget_artifact("ui_layer_footer_with_arrange_buttons", *layers_panel);
  }
  // Back to the default: hidden again, no keys.
  window.apply_tool_palette_customization(ToolPaletteCustomization{});
  settle();
  CHECK(!move_up->isVisible() && !merge_down->isVisible() && animation->isVisible());
  CHECK(!patchy::ui::app_settings().contains(QStringLiteral("layersPanel/hiddenFooterButtons")));
  CHECK(!patchy::ui::app_settings().contains(QStringLiteral("layersPanel/shownFooterButtons")));
}

// GitHub issue 470: the footer's Move Layer Up / Down move one step, to the top or bottom of the
// folder with Shift, and out of the folder with Alt; Merge Down merges down, Shift merges the
// visible layers, Alt merges them into a new layer. Each click is one undo step.
void ui_layer_footer_move_and_merge_buttons_follow_modifiers() {
  using Access = patchy::ui::MainWindowTestAccess;
  CustomizationSettingsGuard settings_guard;
  auto&& [window, canvas] = ShownWindowFixture{};
  auto customization = window.tool_palette_customization();
  customization.hidden_layer_buttons.clear();
  window.apply_tool_palette_customization(customization);
  settle();
  auto* move_up = window.findChild<QPushButton*>(QStringLiteral("layerMoveUpButton"));
  auto* move_down = window.findChild<QPushButton*>(QStringLiteral("layerMoveDownButton"));
  auto* merge_down = window.findChild<QPushButton*>(QStringLiteral("layerMergeDownButton"));
  CHECK(move_up != nullptr && move_down != nullptr && merge_down != nullptr);
  if (move_up == nullptr || move_down == nullptr || merge_down == nullptr) {
    return;
  }
  CHECK(move_up->toolTip().contains(QStringLiteral("Shift-click")));
  CHECK(merge_down->toolTip().contains(QStringLiteral("new layer")));
  // Background, Layer 1, A, Folder (C, D), B.
  auto& doc = Access::document(window);
  const auto& layers = std::as_const(doc).layers();
  CHECK(layers.size() == 2U);
  const auto background_id = layers.front().id();
  const auto paint_id = layers.back().id();
  const auto red = [&doc] {
    return solid_pixels(doc.width(), doc.height(), patchy::PixelFormat::rgba8(), QColor(200, 40, 40, 255));
  };
  const auto a_id = doc.add_pixel_layer("A", red()).id();
  patchy::Layer folder(doc.allocate_layer_id(), "Folder", patchy::LayerKind::Group);
  const auto folder_id = folder.id();
  patchy::Layer c(doc.allocate_layer_id(), "C", red());
  const auto c_id = c.id();
  patchy::Layer d(doc.allocate_layer_id(), "D", red());
  const auto d_id = d.id();
  folder.add_child(std::move(c));
  folder.add_child(std::move(d));
  doc.add_layer(std::move(folder));
  const auto b_id = doc.add_pixel_layer("B", red()).id();
  Access::refresh_layer_ui(window);
  settle();
  using Ids = std::vector<patchy::LayerId>;
  const auto ids_of = [](const std::vector<patchy::Layer>& list) {
    Ids ids;
    for (const auto& layer : list) {
      ids.push_back(layer.id());
    }
    return ids;
  };
  const auto folder_children = [&] {
    const auto* found = std::as_const(doc).find_layer(folder_id);
    return found != nullptr ? ids_of(found->children()) : Ids{};
  };
  const auto click = [&](QPushButton* button, Qt::KeyboardModifiers modifiers, const Ids& selection,
                         patchy::LayerId active) {
    Access::select_layers(window, selection, active);
    QApplication::processEvents();
    const auto depth = Access::active_session_undo_depth(window);
    QTest::mouseClick(button, Qt::LeftButton, modifiers);
    QApplication::processEvents();
    CHECK(Access::active_session_undo_depth(window) == depth + 1);
  };

  click(move_up, Qt::NoModifier, {c_id}, c_id);
  CHECK(folder_children() == (Ids{d_id, c_id}));
  click(move_down, Qt::ShiftModifier, {c_id}, c_id);
  CHECK(folder_children() == (Ids{c_id, d_id}));
  CHECK(ids_of(layers) == (Ids{background_id, paint_id, a_id, folder_id, b_id}));
  click(move_up, Qt::AltModifier, {c_id}, c_id);
  CHECK(ids_of(layers) == (Ids{background_id, paint_id, a_id, folder_id, c_id, b_id}));
  click(move_down, Qt::AltModifier, {d_id}, d_id);
  CHECK(ids_of(layers) == (Ids{background_id, paint_id, a_id, d_id, folder_id, c_id, b_id}));
  CHECK(folder_children().empty());
  // Shift: to the bottom, above the Background; then back to the top.
  click(move_down, Qt::ShiftModifier, {b_id}, b_id);
  CHECK(ids_of(layers) == (Ids{background_id, b_id, paint_id, a_id, d_id, folder_id, c_id}));
  click(move_up, Qt::ShiftModifier, {b_id}, b_id);
  CHECK(ids_of(layers) == (Ids{background_id, paint_id, a_id, d_id, folder_id, c_id, b_id}));
  // Alt on a layer at the root changes nothing.
  Access::select_layers(window, {b_id}, b_id);
  const auto depth = Access::active_session_undo_depth(window);
  QTest::mouseClick(move_up, Qt::LeftButton, Qt::AltModifier);
  QApplication::processEvents();
  CHECK(Access::active_session_undo_depth(window) == depth);
  CHECK(ids_of(layers) == (Ids{background_id, paint_id, a_id, d_id, folder_id, c_id, b_id}));

  // Merge Down: B into C.
  const auto count = layers.size();
  click(merge_down, Qt::NoModifier, {b_id}, b_id);
  CHECK(layers.size() == count - 1U);
  CHECK(std::as_const(doc).find_layer(b_id) == nullptr);
  // Alt: the visible layers merged into a new layer, the rest kept.
  click(merge_down, Qt::AltModifier, {c_id}, c_id);
  CHECK(layers.size() == count);
  CHECK(std::as_const(doc).find_layer(a_id) != nullptr);
  // Shift: Merge Visible in place.
  click(merge_down, Qt::ShiftModifier, {a_id}, a_id);
  CHECK(layers.size() < count - 1U);
}

// GitHub issue 539: Merge Visible and Rasterize Layer are optional footer buttons, hidden by
// default. Merge Visible runs the Layer menu's command and is enabled exactly when it is (off
// with one visible layer); Rasterize opens its menu and is on while any entry is (Trace Image to
// Shapes keeps it on over a pixel layer).
void ui_layer_footer_merge_visible_and_rasterize_buttons() {
  using Access = patchy::ui::MainWindowTestAccess;
  CustomizationSettingsGuard settings_guard;
  const auto listed = [](const auto& ids, const char* wanted) {
    return std::any_of(std::begin(ids), std::end(ids), [wanted](const char* id) { return std::string(id) == wanted; });
  };
  for (const auto* id : {"merge_visible", "rasterize"}) {
    CHECK(listed(patchy::ui::kLayerFooterButtonIds, id));
    CHECK(listed(patchy::ui::kLayerFooterButtonIdsHiddenByDefault, id));
    CHECK(ToolPaletteCustomization{}.hidden_layer_buttons.contains(QLatin1String(id)));
  }
  auto&& [window, canvas] = ShownWindowFixture{};
  settle();
  auto* merge_visible = window.findChild<QPushButton*>(QStringLiteral("layerMergeVisibleButton"));
  auto* rasterize = window.findChild<QToolButton*>(QStringLiteral("layerRasterizeButton"));
  auto* merge_visible_action = require_action(window, "layerMergeVisibleLayersAction");
  auto* rasterize_action = require_action(window, "layerRasterizeAction");
  CHECK(merge_visible != nullptr && rasterize != nullptr && merge_visible_action != nullptr &&
        rasterize_action != nullptr);
  if (merge_visible == nullptr || rasterize == nullptr || merge_visible_action == nullptr ||
      rasterize_action == nullptr) {
    return;
  }
  CHECK(!merge_visible->isVisible() && !rasterize->isVisible());
  CHECK(merge_visible->toolTip() == QStringLiteral("Merge Visible"));
  CHECK(rasterize->toolTip() == QStringLiteral("Rasterize Layer"));
  auto customization = window.tool_palette_customization();
  customization.hidden_layer_buttons.removeAll(QStringLiteral("merge_visible"));
  customization.hidden_layer_buttons.removeAll(QStringLiteral("rasterize"));
  window.apply_tool_palette_customization(customization);
  settle();
  CHECK(merge_visible->isVisible() && rasterize->isVisible());
  CHECK(patchy::ui::app_settings().value(QStringLiteral("layersPanel/shownFooterButtons")).toStringList() ==
        (QStringList{QStringLiteral("merge_visible"), QStringLiteral("rasterize")}));
  // Customize Tool Palette's Layers Panel tab lists both, ticked, under their tooltip names.
  bool ran = false;
  when_customize_dialog_opens([&](QDialog& dialog) {
    ran = true;
    auto* list = dialog.findChild<QListWidget*>(QStringLiteral("layerFooterButtonsList"));
    CHECK(list != nullptr);
    if (list != nullptr) {
      for (const auto& [id, name] :
           {std::pair{"merge_visible", "Merge Visible"}, std::pair{"rasterize", "Rasterize Layer"}}) {
        QListWidgetItem* row = nullptr;
        for (int index = 0; index < list->count(); ++index) {
          if (list->item(index)->data(Qt::UserRole).toString() == QLatin1String(id)) {
            row = list->item(index);
          }
        }
        CHECK(row != nullptr && row->text() == QLatin1String(name) && row->checkState() == Qt::Checked);
        if (row != nullptr) {
          list->scrollToItem(row);
        }
      }
      QApplication::processEvents();
      (void)capture_widget_artifact("ui_tool_palette_customize_merge_visible_and_rasterize", dialog);
    }
    press_dialog_button(dialog, QDialogButtonBox::Cancel);
  });
  window.show_tool_palette_customization(patchy::ui::ToolPaletteCustomizeTab::LayerButtons);
  CHECK(ran);
  settle();
  const std::array<QAction*, 3> rasterize_entries{rasterize_action, require_action(window, "layerRasterizeLayerStyleAction"),
                                                  require_action(window, "layerTraceImageAction")};
  const auto follows_actions = [&] {
    CHECK(merge_visible->isEnabled() == merge_visible_action->isEnabled());
    const bool any_entry = std::any_of(rasterize_entries.begin(), rasterize_entries.end(),
                                       [](const QAction* action) { return action->isEnabled(); });
    CHECK(rasterize->isEnabled() == any_entry);
  };
  // Background and Layer 1: two visible pixel layers, nothing to rasterize.
  auto& doc = Access::document(window);
  const auto& layers = std::as_const(doc).layers();
  CHECK(layers.size() == 2U);
  const auto paint_id = layers.back().id();
  Access::select_layers(window, {paint_id}, paint_id);
  settle();
  follows_actions();
  CHECK(merge_visible->isEnabled() && !rasterize_action->isEnabled() && rasterize->isEnabled());
  // A text layer: Rasterize turns on.
  patchy::Layer text(doc.allocate_layer_id(), "Text: Hi", patchy::LayerKind::Text);
  const auto text_id = text.id();
  text.set_bounds(patchy::Rect{8, 8, 96, 30});
  text.metadata()[patchy::kLayerMetadataText] = "Hi";
  text.metadata()[patchy::kLayerMetadataTextSize] = "24";
  text.metadata()[patchy::kLayerMetadataTextColor] = "#000000";
  doc.add_layer(std::move(text));
  Access::refresh_layer_ui(window);
  Access::select_layers(window, {text_id}, text_id);
  settle();
  follows_actions();
  CHECK(rasterize_action->isEnabled() && rasterize->isEnabled());
  if (auto* panel = window.findChild<QWidget*>(QStringLiteral("layersPanel")); panel != nullptr) {
    (void)capture_widget_artifact("ui_layer_footer_merge_visible_and_rasterize", *panel);
  }
  // Merge Visible: the three visible layers become one, one undo step; then it turns off.
  const auto depth = Access::active_session_undo_depth(window);
  QTest::mouseClick(merge_visible, Qt::LeftButton);
  settle();
  CHECK(Access::active_session_undo_depth(window) == depth + 1);
  CHECK(layers.size() == 1U);
  follows_actions();
  CHECK(!merge_visible->isEnabled());
}

// GitHub issue 539 follow-up: Rasterize Layer is a menu button like Layer Styles. A real press
// (through the window system, as a mouse delivers it) opens the menu at once and runs nothing;
// the menu holds the layer context menu's Rasterize entries, each with its icon showing, and
// picking Rasterize there rasterizes. Merge Down and Merge Visible stay plain buttons without a
// menu.
void ui_layer_footer_rasterize_button_opens_its_menu() {
  using Access = patchy::ui::MainWindowTestAccess;
  CustomizationSettingsGuard settings_guard;
  auto&& [window, canvas] = ShownWindowFixture{};
  auto customization = window.tool_palette_customization();
  customization.hidden_layer_buttons.clear();
  window.apply_tool_palette_customization(customization);
  settle();
  for (const auto* name : {"layerMergeDownButton", "layerMergeVisibleButton"}) {
    auto* button = window.findChild<QPushButton*>(QLatin1String(name));
    CHECK(button != nullptr && button->menu() == nullptr && button->isVisible());
  }
  auto* rasterize = window.findChild<QToolButton*>(QStringLiteral("layerRasterizeButton"));
  auto* panel = window.findChild<QWidget*>(QStringLiteral("layersPanel"));
  CHECK(rasterize != nullptr && panel != nullptr);
  if (rasterize == nullptr || panel == nullptr || rasterize->menu() == nullptr) {
    CHECK(rasterize != nullptr && rasterize->menu() != nullptr);
    return;
  }
  auto* menu = rasterize->menu();
  CHECK(rasterize->popupMode() == QToolButton::InstantPopup);
  CHECK(rasterize->property("layerActionButton").toBool());
  CHECK(menu->objectName() == QStringLiteral("layerRasterizeButtonMenu"));
  QList<QAction*> entries;
  QStringList names;
  for (auto* action : menu->actions()) {
    if (!action->isSeparator()) {
      entries.push_back(action);
      names.push_back(action->objectName());
      // The shared actions, each with its own icon.
      CHECK(action == window.findChild<QAction*>(action->objectName()));
      CHECK(!action->icon().isNull());
    }
  }
  CHECK(names == (QStringList{QStringLiteral("layerRasterizeAction"), QStringLiteral("layerRasterizeLayerStyleAction"),
                              QStringLiteral("layerTraceImageAction")}));
  std::vector<bool> icons_before;
  for (const auto* action : entries) {
    icons_before.push_back(action->isIconVisibleInMenu());
  }

  // Layer 1 under a text layer.
  auto& doc = Access::document(window);
  patchy::Layer text(doc.allocate_layer_id(), "Text: Hi", patchy::LayerKind::Text);
  const auto text_id = text.id();
  text.set_bounds(patchy::Rect{8, 8, 96, 30});
  text.metadata()[patchy::kLayerMetadataText] = "Hi";
  text.metadata()[patchy::kLayerMetadataTextSize] = "24";
  text.metadata()[patchy::kLayerMetadataTextColor] = "#000000";
  doc.add_layer(std::move(text));
  Access::refresh_layer_ui(window);
  Access::select_layers(window, {text_id}, text_id);
  settle();
  CHECK(rasterize->isEnabled());

  auto* handle = window.windowHandle();
  CHECK(handle != nullptr);
  if (handle == nullptr) {
    return;
  }
  const QPoint at = rasterize->mapTo(&window, rasterize->rect().center());
  // Opens the menu with a real press; `in_menu` runs while it is open (exec blocks the press).
  const auto press = [&](const std::function<void()>& in_menu) {
    int shows = 0;
    const auto connection = QObject::connect(menu, &QMenu::aboutToShow, menu, [&] {
      ++shows;
      QTimer::singleShot(0, menu, [&] {
        CHECK(menu->isVisible());
        in_menu();
        if (menu->isVisible()) {
          menu->close();
        }
      });
    });
    // The press itself opens it (a hold-only menu, the first try, opened 200 ms later).
    QTest::mousePress(handle, Qt::LeftButton, Qt::NoModifier, at);
    CHECK(shows == 1);
    QTest::mouseRelease(handle, Qt::LeftButton, Qt::NoModifier, at);
    settle();
    QObject::disconnect(connection);
    CHECK(shows == 1);
    CHECK(!menu->isVisible());
    process_events_for(QApplication::doubleClickInterval() + 50);
  };

  // A press opens the menu at once, every entry showing its icon; closing it runs nothing.
  auto depth = Access::active_session_undo_depth(window);
  press([&] {
    for (const auto* action : entries) {
      CHECK(action->isIconVisibleInMenu());
    }
    // Opened upward from the button, as at the bottom of a screen (the offscreen screen
    // places a menu anywhere).
    menu->move(rasterize->mapToGlobal(QPoint(0, -menu->height())));
    QApplication::processEvents();
    CHECK(capture_theme_mismatch(*menu).isEmpty());
    const auto origin = panel->mapToGlobal(QPoint(0, 0));
    const QRect menu_rect(menu->mapToGlobal(QPoint(0, 0)) - origin, menu->size());
    const QRect all = QRect(QPoint(0, 0), panel->size()).united(menu_rect);
    QImage shot(all.size(), QImage::Format_ARGB32_Premultiplied);
    shot.fill(Qt::transparent);
    {
      QPainter painter(&shot);
      painter.drawPixmap(-all.topLeft(), panel->grab());
      painter.drawPixmap(menu_rect.topLeft() - all.topLeft(), menu->grab());
    }
    save_image_artifact("ui_layer_footer_rasterize_menu", shot);
  });
  CHECK(Access::active_session_undo_depth(window) == depth);
  CHECK(std::as_const(doc).find_layer(text_id)->kind() == patchy::LayerKind::Text);
  // The shared actions' icon setting is back once the menu closes.
  for (qsizetype index = 0; index < entries.size(); ++index) {
    CHECK(entries[index]->isIconVisibleInMenu() == icons_before[static_cast<std::size_t>(index)]);
  }

  // Picking Rasterize in the menu rasterizes the text layer, one undo step.
  press([&] {
    const QRect row = menu->actionGeometry(entries.front());
    QTest::mouseClick(menu, Qt::LeftButton, Qt::NoModifier, row.center());
  });
  CHECK(Access::active_session_undo_depth(window) == depth + 1);
  const auto* rasterized = std::as_const(doc).find_layer(text_id);
  CHECK(rasterized != nullptr && rasterized->kind() == patchy::LayerKind::Pixel);
  // Nothing left to rasterize, but Trace Image to Shapes still works on the pixel layer, so the
  // button stays on.
  CHECK(!entries.front()->isEnabled());
  CHECK(rasterize->isEnabled());
}

QStringList footer_ids(std::initializer_list<const char*> ids) {
  QStringList out;
  for (const auto* id : ids) {
    out.push_back(QString::fromLatin1(id));
  }
  return out;
}

QStringList default_footer_order() {
  QStringList out;
  for (const auto* id : patchy::ui::kLayerFooterButtonIds) {
    out.push_back(QString::fromLatin1(id));
  }
  return out;
}

// GitHub issue 471: the stored footer order keeps every known button once; ids this build does
// not know drop, and a button the order leaves out (one added later) follows the button it
// follows by default, or leads when it leads by default. The default order stores no key.
void ui_layer_footer_order_resolves_unknown_and_missing_ids() {
  CustomizationSettingsGuard settings_guard;
  using patchy::ui::resolved_layer_footer_order;
  const auto moved_delete = footer_ids({"delete", "new_layer", "new_folder", "new_adjustment", "layer_styles",
                                        "add_mask", "duplicate", "move_up", "move_down", "merge_down",
                                        "merge_visible", "rasterize", "animation"});
  CHECK(resolved_layer_footer_order({}) == default_footer_order());
  CHECK(resolved_layer_footer_order(footer_ids({"delete", "future_button", "new_layer", "delete"})) == moved_delete);
  CHECK(resolved_layer_footer_order(footer_ids({"delete", "new_folder"})) ==
        footer_ids({"new_layer", "delete", "new_folder", "new_adjustment", "layer_styles", "add_mask", "duplicate",
                    "move_up", "move_down", "merge_down", "merge_visible", "rasterize", "animation"}));
  CHECK(resolved_layer_footer_order(footer_ids({"animation", "duplicate"})) ==
        footer_ids({"new_layer", "new_folder", "new_adjustment", "layer_styles", "add_mask", "animation", "delete",
                    "duplicate", "move_up", "move_down", "merge_down", "merge_visible", "rasterize"}));

  ToolPaletteCustomization value;
  value.layer_button_order = default_footer_order();
  CHECK(patchy::ui::normalized_tool_palette_customization(value) == ToolPaletteCustomization{});
  value.layer_button_order = footer_ids({"delete", "new_layer"});
  CHECK(patchy::ui::normalized_tool_palette_customization(value).layer_button_order == moved_delete);
  // Delete Layer alone: every other button goes back before it, the default order.
  value.layer_button_order = footer_ids({"delete"});
  CHECK(patchy::ui::normalized_tool_palette_customization(value).layer_button_order.isEmpty());

  const auto key = QStringLiteral("layersPanel/footerButtonOrder");
  patchy::ui::app_settings().setValue(key, footer_ids({"delete", "future_button", "new_layer"}));
  const auto stored = patchy::ui::stored_tool_palette_customization();
  CHECK(stored.layer_button_order == moved_delete);
  patchy::ui::set_stored_tool_palette_customization(stored);
  CHECK(patchy::ui::app_settings().value(key).toStringList() == moved_delete);
  patchy::ui::set_stored_tool_palette_customization(ToolPaletteCustomization{});
  CHECK(!patchy::ui::app_settings().contains(key));
}

// GitHub issue 471: the footer and its right-click menu follow the stored order, a new window
// starts with it, and the default order puts them back.
void ui_layer_footer_follows_stored_order() {
  CustomizationSettingsGuard settings_guard;
  auto&& [window, canvas] = ShownWindowFixture{};
  settle();
  auto* new_button = window.findChild<QPushButton*>(QStringLiteral("layerNewButton"));
  auto* delete_button = window.findChild<QPushButton*>(QStringLiteral("layerDeleteButton"));
  auto* duplicate = window.findChild<QPushButton*>(QStringLiteral("layerDuplicateButton"));
  CHECK(new_button != nullptr && delete_button != nullptr && duplicate != nullptr);
  if (new_button == nullptr || delete_button == nullptr || duplicate == nullptr) {
    return;
  }
  CHECK(new_button->x() < duplicate->x() && duplicate->x() < delete_button->x());
  const auto expected = footer_ids({"delete", "duplicate", "new_layer", "new_folder", "new_adjustment", "layer_styles",
                                    "add_mask", "move_up", "move_down", "merge_down",
                                    "merge_visible", "rasterize", "animation"});
  auto customization = window.tool_palette_customization();
  customization.layer_button_order = expected;
  window.apply_tool_palette_customization(customization);
  settle();
  CHECK(window.tool_palette_customization().layer_button_order == expected);
  CHECK(patchy::ui::app_settings().value(QStringLiteral("layersPanel/footerButtonOrder")).toStringList() == expected);
  // Still one row, in the new order.
  CHECK(delete_button->y() == new_button->y() && duplicate->y() == new_button->y());
  CHECK(delete_button->x() < duplicate->x() && duplicate->x() < new_button->x());
  if (auto* layers_panel = duplicate->parentWidget(); layers_panel != nullptr) {
    (void)capture_widget_artifact("ui_layer_footer_reordered", *layers_panel);
  }
  // The right-click menu lists the buttons in the same order.
  const QPoint position = duplicate->rect().center();
  QContextMenuEvent event(QContextMenuEvent::Mouse, position, duplicate->mapToGlobal(position));
  QApplication::sendEvent(duplicate, &event);
  QApplication::processEvents();
  auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
  CHECK(menu != nullptr && menu->objectName() == QStringLiteral("layerFooterButtonsMenu"));
  if (menu != nullptr) {
    CHECK(menu->actions().size() > 2 &&
          menu->actions()[0]->objectName() == QStringLiteral("layerFooterToggle_delete") &&
          menu->actions()[1]->objectName() == QStringLiteral("layerFooterToggle_duplicate"));
    menu->close();
  }
  settle();
  // A new window (a restart) starts with the stored order.
  {
    patchy::ui::MainWindow second;
    CHECK(second.tool_palette_customization().layer_button_order == expected);
  }
  // Back to the default order: the footer too, and no key.
  window.apply_tool_palette_customization(ToolPaletteCustomization{});
  settle();
  CHECK(new_button->x() < duplicate->x() && duplicate->x() < delete_button->x());
  CHECK(!patchy::ui::app_settings().contains(QStringLiteral("layersPanel/footerButtonOrder")));
}

// GitHub issue 471: the dialog's Layers buttons tab lists the buttons in footer order; Move Up
// and Move Down move the current row (off where they would do nothing), OK applies the order to
// the footer, and Reset to Default restores the default order.
void ui_tool_palette_dialog_reorders_layer_buttons() {
  CustomizationSettingsGuard settings_guard;
  auto&& [window, canvas] = ShownWindowFixture{};
  settle();
  const auto row_ids = [](QListWidget& list) {
    QStringList ids;
    for (int index = 0; index < list.count(); ++index) {
      ids.push_back(list.item(index)->data(Qt::UserRole).toString());
    }
    return ids;
  };
  bool ran = false;
  when_customize_dialog_opens([&](QDialog& dialog) {
    ran = true;
    auto* list = dialog.findChild<QListWidget*>(QStringLiteral("layerFooterButtonsList"));
    auto* up = dialog.findChild<QPushButton*>(QStringLiteral("layerFooterOrderUpButton"));
    auto* down = dialog.findChild<QPushButton*>(QStringLiteral("layerFooterOrderDownButton"));
    CHECK(list != nullptr && up != nullptr && down != nullptr);
    if (list == nullptr || up == nullptr || down == nullptr) {
      dialog.reject();
      return;
    }
    CHECK(list->isVisible());
    CHECK(row_ids(*list) == default_footer_order());
    CHECK(!up->isEnabled() && !down->isEnabled());
    list->setCurrentRow(list->count() - 1);
    CHECK(up->isEnabled() && !down->isEnabled());
    // Delete Layer to the top, then Duplicate Layer one step up.
    for (int step = 0; step < list->count() - 1; ++step) {
      up->click();
    }
    CHECK(list->currentRow() == 0 && !up->isEnabled() && down->isEnabled());
    list->setCurrentRow(row_ids(*list).indexOf(QStringLiteral("duplicate")));
    up->click();
    down->click();
    up->click();
    // Show Move Layer Up too, so the picture shows an optional button in its place.
    list->item(row_ids(*list).indexOf(QStringLiteral("move_up")))->setCheckState(Qt::Checked);
    QApplication::processEvents();
    CHECK(row_ids(*list) == footer_ids({"delete", "new_layer", "new_folder", "new_adjustment", "layer_styles",
                                        "duplicate", "add_mask", "move_up", "move_down", "merge_down",
                                        "merge_visible", "rasterize", "animation"}));
    (void)capture_widget_artifact("ui_tool_palette_customize_layer_button_order", dialog);
    press_dialog_button(dialog, QDialogButtonBox::Ok);
  });
  window.show_tool_palette_customization(patchy::ui::ToolPaletteCustomizeTab::LayerButtons);
  CHECK(ran);
  settle();
  const auto expected = footer_ids({"delete", "new_layer", "new_folder", "new_adjustment", "layer_styles", "duplicate",
                                    "add_mask", "move_up", "move_down", "merge_down",
                                    "merge_visible", "rasterize", "animation"});
  CHECK(window.tool_palette_customization().layer_button_order == expected);
  CHECK(!window.tool_palette_customization().hidden_layer_buttons.contains(QStringLiteral("move_up")));
  auto* new_button = window.findChild<QPushButton*>(QStringLiteral("layerNewButton"));
  auto* delete_button = window.findChild<QPushButton*>(QStringLiteral("layerDeleteButton"));
  auto* duplicate = window.findChild<QPushButton*>(QStringLiteral("layerDuplicateButton"));
  auto* add_mask = window.findChild<QPushButton*>(QStringLiteral("layerAddMaskButton"));
  auto* move_up = window.findChild<QPushButton*>(QStringLiteral("layerMoveUpButton"));
  CHECK(new_button != nullptr && delete_button != nullptr && duplicate != nullptr && add_mask != nullptr &&
        move_up != nullptr);
  if (new_button != nullptr && delete_button != nullptr && duplicate != nullptr && add_mask != nullptr &&
      move_up != nullptr) {
    const auto visual_index = [](QWidget* button) { return button->y() * 10000 + button->x(); };
    CHECK(visual_index(delete_button) < visual_index(new_button));
    CHECK(visual_index(duplicate) < visual_index(add_mask));
    CHECK(visual_index(add_mask) < visual_index(move_up));
    if (auto* layers_panel = duplicate->parentWidget(); layers_panel != nullptr) {
      (void)capture_widget_artifact("ui_layer_footer_reordered_from_dialog", *layers_panel);
    }
  }

  // Reopened, the list follows the footer; Reset to Default restores the order, OK stores no key.
  ran = false;
  when_customize_dialog_opens([&](QDialog& dialog) {
    ran = true;
    auto* list = dialog.findChild<QListWidget*>(QStringLiteral("layerFooterButtonsList"));
    CHECK(list != nullptr);
    if (list == nullptr) {
      dialog.reject();
      return;
    }
    CHECK(row_ids(*list) == expected);
    dialog.findChild<QPushButton*>(QStringLiteral("toolPaletteCustomizeResetButton"))->click();
    QApplication::processEvents();
    CHECK(row_ids(*list) == default_footer_order());
    CHECK(list->item(row_ids(*list).indexOf(QStringLiteral("move_up")))->checkState() == Qt::Unchecked);
    press_dialog_button(dialog, QDialogButtonBox::Ok);
  });
  window.show_tool_palette_customization(patchy::ui::ToolPaletteCustomizeTab::LayerButtons);
  CHECK(ran);
  settle();
  CHECK(window.tool_palette_customization() == ToolPaletteCustomization{});
  CHECK(!patchy::ui::app_settings().contains(QStringLiteral("layersPanel/footerButtonOrder")));
  if (new_button != nullptr && delete_button != nullptr) {
    CHECK(new_button->x() < delete_button->x());
  }
}

// Each Layers buttons preset's shown buttons, left to right (GitHub issue 471).
QStringList preset_shown_buttons(patchy::ui::LayerFooterPreset preset) {
  using patchy::ui::LayerFooterPreset;
  switch (preset) {
    case LayerFooterPreset::Patchy:
      break;
    case LayerFooterPreset::Photoshop:
      return footer_ids({"layer_styles", "add_mask", "new_adjustment", "new_folder", "new_layer", "delete"});
    case LayerFooterPreset::AffinityPhoto:
      return footer_ids(
          {"duplicate", "add_mask", "new_adjustment", "layer_styles", "new_folder", "new_layer", "delete"});
    case LayerFooterPreset::Gimp:
      return footer_ids(
          {"new_layer", "new_folder", "move_up", "move_down", "duplicate", "merge_down", "add_mask", "delete"});
    case LayerFooterPreset::Krita:
      return footer_ids({"new_layer", "new_folder", "duplicate", "new_adjustment", "add_mask", "move_down", "move_up",
                         "layer_styles", "delete"});
  }
  return footer_ids(
      {"new_layer", "new_folder", "new_adjustment", "layer_styles", "add_mask", "duplicate", "animation", "delete"});
}

// GitHub issue 471: a preset shows its buttons in its order and hides the rest after them;
// Patchy is the default; a customization showing a preset's buttons in its order matches it,
// after a store and reload too, and any other arrangement matches none (Custom).
void ui_layer_footer_presets_set_order_and_shown_buttons() {
  CustomizationSettingsGuard settings_guard;
  using patchy::ui::LayerFooterPreset;
  const auto known = default_footer_order();
  for (const auto preset : patchy::ui::kLayerFooterPresets) {
    ToolPaletteCustomization value;
    value.hidden_layer_buttons.push_back(QStringLiteral("future_button"));
    patchy::ui::apply_layer_footer_preset(value, preset);
    const auto expected = preset_shown_buttons(preset);
    CHECK(patchy::ui::shown_layer_footer_buttons(value) == expected);
    for (const auto& id : known) {
      CHECK(value.hidden_layer_buttons.contains(id) != expected.contains(id));
    }
    // Ids this build does not know survive a preset.
    CHECK(value.hidden_layer_buttons.contains(QStringLiteral("future_button")));
    CHECK(patchy::ui::matching_layer_footer_preset(value) == preset);
    patchy::ui::set_stored_tool_palette_customization(value);
    CHECK(patchy::ui::matching_layer_footer_preset(patchy::ui::stored_tool_palette_customization()) == preset);
  }
  ToolPaletteCustomization patchy_preset;
  patchy::ui::apply_layer_footer_preset(patchy_preset, LayerFooterPreset::Krita);
  patchy::ui::apply_layer_footer_preset(patchy_preset, LayerFooterPreset::Patchy);
  CHECK(patchy::ui::normalized_tool_palette_customization(patchy_preset) == ToolPaletteCustomization{});
  CHECK(patchy::ui::matching_layer_footer_preset(ToolPaletteCustomization{}) == LayerFooterPreset::Patchy);
  // Krita with Animation shown too, or two of its buttons swapped: Custom.
  ToolPaletteCustomization krita;
  patchy::ui::apply_layer_footer_preset(krita, LayerFooterPreset::Krita);
  auto with_animation = krita;
  with_animation.hidden_layer_buttons.removeAll(QStringLiteral("animation"));
  CHECK(!patchy::ui::matching_layer_footer_preset(with_animation).has_value());
  auto swapped = krita;
  swapped.layer_button_order.swapItemsAt(2, 3);
  CHECK(!patchy::ui::matching_layer_footer_preset(swapped).has_value());
  // Hidden buttons' places do not matter: the footer looks the same.
  auto hidden_moved = krita;
  hidden_moved.layer_button_order.move(hidden_moved.layer_button_order.size() - 1, 0);
  CHECK(patchy::ui::matching_layer_footer_preset(hidden_moved) == LayerFooterPreset::Krita);
}

// GitHub issue 471: the Layers buttons tab's Preset combo shows the matching preset when the
// dialog opens, picking one sets the rows' order and ticks, a manual change makes it Custom,
// OK applies the preset to the footer (kept after a restart), and Reset to Default is Patchy.
void ui_tool_palette_dialog_layer_button_presets() {
  CustomizationSettingsGuard settings_guard;
  using patchy::ui::LayerFooterPreset;
  auto&& [window, canvas] = ShownWindowFixture{};
  settle();
  const auto shown_rows = [](QListWidget& list) {
    QStringList ids;
    for (int index = 0; index < list.count(); ++index) {
      if (list.item(index)->checkState() == Qt::Checked) {
        ids.push_back(list.item(index)->data(Qt::UserRole).toString());
      }
    }
    return ids;
  };
  const auto pick = [](QComboBox& combo, const char* token) {
    const int index = combo.findData(QString::fromLatin1(token));
    CHECK(index >= 0);
    combo.setCurrentIndex(index);
    emit combo.activated(index);
    QApplication::processEvents();
  };
  bool ran = false;
  when_customize_dialog_opens([&](QDialog& dialog) {
    ran = true;
    auto* list = dialog.findChild<QListWidget*>(QStringLiteral("layerFooterButtonsList"));
    auto* combo = dialog.findChild<QComboBox*>(QStringLiteral("layerFooterPresetCombo"));
    auto* up = dialog.findChild<QPushButton*>(QStringLiteral("layerFooterOrderUpButton"));
    CHECK(list != nullptr && combo != nullptr && up != nullptr);
    if (list == nullptr || combo == nullptr || up == nullptr) {
      dialog.reject();
      return;
    }
    CHECK(combo->count() == 6);
    CHECK(combo->currentData().toString() == QStringLiteral("patchy"));
    const std::pair<const char*, LayerFooterPreset> presets[] = {{"photoshop", LayerFooterPreset::Photoshop},
                                                                 {"affinity_photo", LayerFooterPreset::AffinityPhoto},
                                                                 {"gimp", LayerFooterPreset::Gimp},
                                                                 {"krita", LayerFooterPreset::Krita},
                                                                 {"patchy", LayerFooterPreset::Patchy}};
    for (const auto& [token, preset] : presets) {
      pick(*combo, token);
      CHECK(shown_rows(*list) == preset_shown_buttons(preset));
      CHECK(combo->currentData().toString() == QString::fromLatin1(token));
    }
    // A tick, then a move: Custom; ticking back matches the preset again.
    pick(*combo, "photoshop");
    (void)capture_widget_artifact("ui_tool_palette_customize_layer_button_presets", dialog);
    auto* animation = list->findItems(QStringLiteral("Animation Preview"), Qt::MatchExactly).value(0);
    CHECK(animation != nullptr);
    if (animation != nullptr) {
      animation->setCheckState(Qt::Checked);
      CHECK(combo->currentData().toString() == QStringLiteral("custom"));
      animation->setCheckState(Qt::Unchecked);
      CHECK(combo->currentData().toString() == QStringLiteral("photoshop"));
    }
    list->setCurrentRow(1);
    up->click();
    CHECK(combo->currentData().toString() == QStringLiteral("custom"));
    pick(*combo, "krita");
    CHECK(combo->currentData().toString() == QStringLiteral("krita"));
    press_dialog_button(dialog, QDialogButtonBox::Ok);
  });
  window.show_tool_palette_customization(patchy::ui::ToolPaletteCustomizeTab::LayerButtons);
  CHECK(ran);
  settle();
  CHECK(patchy::ui::matching_layer_footer_preset(window.tool_palette_customization()) == LayerFooterPreset::Krita);
  // The footer: Krita's nine (#471 follow-up), in one row; the rest hidden.
  std::vector<QWidget*> shown;
  for (const auto* name : {"layerNewButton", "layerNewFolderButton", "layerDuplicateButton", "layerNewAdjustmentButton",
                           "layerAddMaskButton", "layerMoveDownButton", "layerMoveUpButton", "layerStylesButton",
                           "layerDeleteButton"}) {
    auto* button = window.findChild<QWidget*>(QLatin1String(name));
    CHECK(button != nullptr && button->isVisible());
    shown.push_back(button);
  }
  for (std::size_t index = 1; index < shown.size(); ++index) {
    if (shown[index - 1] != nullptr && shown[index] != nullptr) {
      CHECK(shown[index - 1]->y() == shown[index]->y() && shown[index - 1]->x() < shown[index]->x());
    }
  }
  for (const auto* name : {"layerMergeDownButton", "layerAnimationButton"}) {
    auto* button = window.findChild<QWidget*>(QLatin1String(name));
    CHECK(button != nullptr && !button->isVisible());
  }
  if (!shown.empty() && shown.front() != nullptr && shown.front()->parentWidget() != nullptr) {
    (void)capture_widget_artifact("ui_layer_footer_krita_preset", *shown.front()->parentWidget());
  }
  // A restart keeps it.
  {
    patchy::ui::MainWindow second;
    CHECK(patchy::ui::matching_layer_footer_preset(second.tool_palette_customization()) == LayerFooterPreset::Krita);
  }
  // Reopened, the combo names Krita; Reset to Default is Patchy.
  ran = false;
  when_customize_dialog_opens([&](QDialog& dialog) {
    ran = true;
    auto* combo = dialog.findChild<QComboBox*>(QStringLiteral("layerFooterPresetCombo"));
    CHECK(combo != nullptr && combo->currentData().toString() == QStringLiteral("krita"));
    dialog.findChild<QPushButton*>(QStringLiteral("toolPaletteCustomizeResetButton"))->click();
    QApplication::processEvents();
    CHECK(combo == nullptr || combo->currentData().toString() == QStringLiteral("patchy"));
    press_dialog_button(dialog, QDialogButtonBox::Ok);
  });
  window.show_tool_palette_customization(patchy::ui::ToolPaletteCustomizeTab::LayerButtons);
  CHECK(ran);
  settle();
  CHECK(window.tool_palette_customization() == ToolPaletteCustomization{});
}

// GitHub issue 470 follow-up: Move Layer Up and Down (a layer, a triangle above or below it) and
// Merge Down (a three-layer stack, the bottom layer in the accent) draw in the icon ink and
// accent roles, so both show at every footer icon size in Dark and Light. Saves the sheet the
// card shows.
void ui_layer_footer_arrange_icons_draw_in_both_schemes() {
  const char* const names[] = {"layer-up", "layer-down", "merge-down", "merge-visible", "rasterize"};
  constexpr int kCell = 44;
  QImage sheet(QSize(kCell * static_cast<int>(std::size(names) * patchy::ui::kToolbarIconSizes.size()), kCell * 2),
               QImage::Format_ARGB32_Premultiplied);
  int scheme_row = 0;
  for (const auto scheme : {patchy::ui::ColorSchemePreference::Dark, patchy::ui::ColorSchemePreference::Light}) {
    ColorSchemeRestorer restore(scheme);
    const auto& palette = patchy::ui::theme();
    QPainter sheet_painter(&sheet);
    sheet_painter.fillRect(QRect(0, scheme_row * kCell, sheet.width(), kCell), palette.panel_bg);
    int column = 0;
    for (const auto size : patchy::ui::kToolbarIconSizes) {
      const auto extent = patchy::ui::tool_palette_length(24, patchy::ui::toolbar_icon_size_factor(size));
      for (const auto* name : names) {
        const auto image =
            patchy::ui::simple_icon(QString::fromLatin1(name)).pixmap(QSize(extent, extent), 1.0).toImage();
        CHECK(image.width() == extent);
        // Some pixels near the ink and some near the accent, at every size.
        int ink = 0;
        int accent = 0;
        const auto near = [](const QColor& a, const QColor& b) {
          return std::abs(a.red() - b.red()) + std::abs(a.green() - b.green()) + std::abs(a.blue() - b.blue()) < 60;
        };
        for (int y = 0; y < image.height(); ++y) {
          for (int x = 0; x < image.width(); ++x) {
            const auto color = image.pixelColor(x, y);
            if (color.alpha() < 200) {
              continue;
            }
            ink += near(color, palette.icon_ink) ? 1 : 0;
            accent += near(color, palette.icon_accent) ? 1 : 0;
          }
        }
        if (ink < 4 || accent < 2) {
          std::cout << "[INFO] " << name << " at " << extent << " px: " << ink << " ink, " << accent
                    << " accent pixels\n";
        }
        CHECK(ink >= 4);
        CHECK(accent >= 2);
        sheet_painter.drawImage(QPoint(column * kCell + (kCell - extent) / 2, scheme_row * kCell + (kCell - extent) / 2),
                                image);
        ++column;
      }
    }
    sheet_painter.end();
    // The footer with every button shown, the three among the others.
    {
      CustomizationSettingsGuard settings_guard;
      auto&& [window, canvas] = ShownWindowFixture{};
      ToolPaletteCustomization customization;
      customization.hidden_layer_buttons.clear();
      window.apply_tool_palette_customization(customization);
      settle();
      if (auto* panel = window.findChild<QWidget*>(QStringLiteral("layersPanel")); panel != nullptr) {
        (void)capture_widget_artifact(
            std::string("ui_layer_footer_arrange_buttons_") +
                (scheme == patchy::ui::ColorSchemePreference::Dark ? "dark" : "light"),
            *panel);
      }
    }
    ++scheme_row;
  }
  save_image_artifact("ui_layer_footer_arrange_icons", sheet);
}

// The visible Layers footer buttons in reading order, and each one's rect in the panel.
struct FooterButtonRect {
  QAbstractButton* button;
  QRect rect;
};

std::vector<FooterButtonRect> visible_footer_buttons(QWidget& panel) {
  std::vector<FooterButtonRect> buttons;
  for (auto* button : panel.findChildren<QAbstractButton*>()) {
    if (button->property("layerActionButton").toBool() && button->isVisible()) {
      buttons.push_back({button, QRect(button->mapTo(&panel, QPoint()), button->size())});
    }
  }
  // Reading order, which is footer order.
  std::sort(buttons.begin(), buttons.end(), [](const auto& a, const auto& b) {
    return a.rect.y() != b.rect.y() ? a.rect.y() < b.rect.y() : a.rect.x() < b.rect.x();
  });
  return buttons;
}

// GitHub issue 213 follow-up (replacing #199's balanced rows): at every icon size and with the
// default eight or all eleven buttons, the footer keeps every button on one row while it fits;
// as the column narrows, buttons move to the next row one at a time, every row but the last
// full, in the same columns; nothing overlaps or leaves the panel, down to the compact width.
void ui_layer_footer_wraps_one_button_at_a_time() {
  CustomizationSettingsGuard settings_guard;
  for (const auto size : patchy::ui::kToolbarIconSizes) {
    patchy::ui::set_stored_layer_footer_icon_size(size);
    for (const bool all_buttons : {false, true}) {
      auto&& [window, canvas] = ShownWindowFixture{};
      // Applying stores it, so the default is applied too.
      ToolPaletteCustomization customization;
      if (all_buttons) {
        customization.hidden_layer_buttons.clear();
      }
      window.apply_tool_palette_customization(customization);
      settle();
      auto* panel = window.findChild<QWidget*>(QStringLiteral("layersPanel"));
      CHECK(panel != nullptr);
      if (panel == nullptr) {
        return;
      }
      const auto expected_count = all_buttons ? 13 : 8;
      const auto opening = patchy::ui::MainWindowTestAccess::right_dock_content_minimum_width(window);
      const auto compact = patchy::ui::MainWindowTestAccess::right_dock_compact_width(window);
      int previous_first_row = expected_count;
      int checked_widths = 0;
      for (int width = opening; width >= compact; width -= 5) {
        patchy::ui::MainWindowTestAccess::set_right_dock_stack_width(window, width);
        for (int pass = 0; pass < 3; ++pass) {
          QApplication::processEvents();
        }
        const auto buttons = visible_footer_buttons(*panel);
        CHECK(static_cast<int>(buttons.size()) == expected_count);
        if (static_cast<int>(buttons.size()) != expected_count) {
          return;
        }
        ++checked_widths;
        // Rows by y: every row but the last as full as the first, columns lined up.
        std::vector<int> row_counts;
        int row_y = -1;
        for (const auto& entry : buttons) {
          CHECK(panel->rect().contains(entry.rect));
          CHECK(entry.rect.width() >= entry.button->minimumWidth());
          CHECK(entry.rect.width() >= entry.button->iconSize().width());
          if (entry.rect.y() != row_y) {
            row_counts.push_back(0);
            row_y = entry.rect.y();
          }
          ++row_counts.back();
        }
        for (std::size_t index = 1; index < buttons.size(); ++index) {
          CHECK(!buttons[index - 1].rect.intersects(buttons[index].rect));
        }
        for (std::size_t row = 0; row + 1 < row_counts.size(); ++row) {
          CHECK(row_counts[row] == row_counts.front());
        }
        for (std::size_t index = static_cast<std::size_t>(row_counts.front()); index < buttons.size(); ++index) {
          CHECK(buttons[index].rect.x() == buttons[index % static_cast<std::size_t>(row_counts.front())].rect.x());
        }
        if (width == opening) {
          CHECK(row_counts.size() == 1);
        }
        // One button at a time: a 5 px step is narrower than any button.
        CHECK(row_counts.front() <= previous_first_row);
        CHECK(row_counts.front() >= previous_first_row - 1);
        previous_first_row = row_counts.front();
      }
      CHECK(checked_widths > 3);
      // At Normal the compact width wraps the footer (smaller buttons may still fit one row).
      if (size == patchy::ui::ToolbarIconSize::Normal) {
        CHECK(previous_first_row < expected_count);
      }
      std::cout << "[INFO] footer " << patchy::ui::toolbar_icon_size_token(size).toStdString() << ", "
                << expected_count << " buttons: opens at " << opening << " px, compact " << compact << " px with "
                << previous_first_row << " per row\n";
      if (size == patchy::ui::ToolbarIconSize::Normal && all_buttons) {
        (void)capture_widget_artifact("ui_layer_footer_wraps_at_compact_width", *panel);
      }
    }
  }
}

// GitHub issue 213 follow-up: the Layers buttons tab's icon size combo offers the toolbar icon
// size's steps, shows a pick on the footer at once, Cancel puts it back, OK stores it under its
// own key (never the toolbar's) and a new window starts with it; Normal stores no key.
void ui_layer_footer_icon_size_persists_and_applies() {
  CustomizationSettingsGuard settings_guard;
  using patchy::ui::ToolbarIconSize;
  auto&& [window, canvas] = ShownWindowFixture{};
  settle();
  auto* new_button = window.findChild<QPushButton*>(QStringLiteral("layerNewButton"));
  auto* styles_button = window.findChild<QToolButton*>(QStringLiteral("layerStylesButton"));
  CHECK(new_button != nullptr && styles_button != nullptr);
  if (new_button == nullptr || styles_button == nullptr) {
    return;
  }
  CHECK(new_button->iconSize() == QSize(24, 24));
  const auto normal_height = new_button->height();
  const auto frame = normal_height - 34;
  // The button lengths at a size: the Normal content lengths times its factor, plus the frame.
  const auto check_size = [&](QAbstractButton& button, ToolbarIconSize size) {
    const auto factor = patchy::ui::toolbar_icon_size_factor(size);
    const auto icon = patchy::ui::tool_palette_length(24, factor);
    CHECK(button.iconSize() == QSize(icon, icon));
    CHECK(button.minimumHeight() == patchy::ui::tool_palette_length(34, factor) + frame);
    CHECK(button.maximumHeight() == button.minimumHeight());
    CHECK(button.minimumWidth() == patchy::ui::tool_palette_length(30, factor) + frame);
    CHECK(button.maximumWidth() == patchy::ui::tool_palette_length(38, factor) + frame);
  };
  check_size(*new_button, ToolbarIconSize::Normal);
  check_size(*styles_button, ToolbarIconSize::Normal);
  const auto pick = [](QDialog& dialog, ToolbarIconSize size) {
    auto* combo = dialog.findChild<QComboBox*>(QStringLiteral("layerFooterIconSizeCombo"));
    CHECK(combo != nullptr);
    if (combo != nullptr) {
      combo->setCurrentIndex(combo->findData(patchy::ui::toolbar_icon_size_token(size)));
    }
    settle();
  };
  // Picked, it shows at once; Cancel restores Normal and stores nothing.
  bool ran = false;
  when_customize_dialog_opens([&](QDialog& dialog) {
    ran = true;
    auto* combo = dialog.findChild<QComboBox*>(QStringLiteral("layerFooterIconSizeCombo"));
    auto* palette_combo = dialog.findChild<QComboBox*>(QStringLiteral("toolPaletteIconSizeCombo"));
    CHECK(combo != nullptr && palette_combo != nullptr);
    if (combo != nullptr && palette_combo != nullptr) {
      CHECK(combo->count() == palette_combo->count());
      for (int index = 0; index < combo->count(); ++index) {
        CHECK(combo->itemData(index) == palette_combo->itemData(index));
        CHECK(combo->itemText(index) == palette_combo->itemText(index));
      }
      CHECK(combo->currentData().toString() == QStringLiteral("normal"));
    }
    pick(dialog, ToolbarIconSize::Biggest);
    check_size(*new_button, ToolbarIconSize::Biggest);
    check_size(*styles_button, ToolbarIconSize::Biggest);
    (void)capture_widget_artifact("ui_layer_footer_icon_size_biggest", *new_button->parentWidget());
    press_dialog_button(dialog, QDialogButtonBox::Cancel);
  });
  window.show_tool_palette_customization(patchy::ui::ToolPaletteCustomizeTab::LayerButtons);
  CHECK(ran);
  settle();
  check_size(*new_button, ToolbarIconSize::Normal);
  CHECK(!patchy::ui::app_settings().contains(QStringLiteral("layersPanel/footerIconSize")));
  // OK stores it under its own key; the toolbar icon size stays.
  ran = false;
  when_customize_dialog_opens([&](QDialog& dialog) {
    ran = true;
    pick(dialog, ToolbarIconSize::Small);
    press_dialog_button(dialog, QDialogButtonBox::Ok);
  });
  window.show_tool_palette_customization(patchy::ui::ToolPaletteCustomizeTab::LayerButtons);
  CHECK(ran);
  settle();
  check_size(*new_button, ToolbarIconSize::Small);
  CHECK(patchy::ui::app_settings().value(QStringLiteral("layersPanel/footerIconSize")).toString() ==
        QStringLiteral("small"));
  CHECK(!patchy::ui::app_settings().contains(QStringLiteral("preferences/toolbarIconSize")));
  CHECK(patchy::ui::stored_toolbar_icon_size() == ToolbarIconSize::Normal);
  {
    patchy::ui::MainWindow second;
    auto* second_button = second.findChild<QPushButton*>(QStringLiteral("layerNewButton"));
    CHECK(second_button != nullptr);
    if (second_button != nullptr) {
      const auto icon = patchy::ui::tool_palette_length(24, patchy::ui::toolbar_icon_size_factor(ToolbarIconSize::Small));
      CHECK(second_button->iconSize() == QSize(icon, icon));
    }
  }
  // Reopened, the combo shows it; Reset to Default and OK go back to Normal and drop the key.
  ran = false;
  when_customize_dialog_opens([&](QDialog& dialog) {
    ran = true;
    auto* combo = dialog.findChild<QComboBox*>(QStringLiteral("layerFooterIconSizeCombo"));
    CHECK(combo != nullptr && combo->currentData().toString() == QStringLiteral("small"));
    auto* reset = dialog.findChild<QPushButton*>(QStringLiteral("toolPaletteCustomizeResetButton"));
    CHECK(reset != nullptr);
    if (reset != nullptr) {
      reset->click();
    }
    settle();
    CHECK(combo != nullptr && combo->currentData().toString() == QStringLiteral("normal"));
    press_dialog_button(dialog, QDialogButtonBox::Ok);
  });
  window.show_tool_palette_customization(patchy::ui::ToolPaletteCustomizeTab::LayerButtons);
  CHECK(ran);
  settle();
  check_size(*new_button, ToolbarIconSize::Normal);
  CHECK(!patchy::ui::app_settings().contains(QStringLiteral("layersPanel/footerIconSize")));
}

// The leftmost inked x (image pixels) of the last run of ink in `band`, scanning right to left:
// the shortcut key at a row's right end. Runs closer than `max_gap` px belong together.
int key_ink_left(const QImage& image, const QRect& band, int max_gap) {
  const QColor background = image.pixelColor(band.right(), band.center().y());
  const auto inked = [&](int x) {
    for (int y = std::max(0, band.top()); y <= std::min(band.bottom(), image.height() - 1); ++y) {
      const QColor color = image.pixelColor(x, y);
      if (std::abs(color.red() - background.red()) + std::abs(color.green() - background.green()) +
              std::abs(color.blue() - background.blue()) >
          90) {
        return true;
      }
    }
    return false;
  };
  int left = -1;
  int gap = 0;
  for (int x = std::min(band.right(), image.width() - 1); x >= std::max(0, band.left()); --x) {
    if (inked(x)) {
      left = x;
      gap = 0;
    } else if (left >= 0 && ++gap > max_gap) {
      break;
    }
  }
  return left;
}

// GitHub issue 499: a tool group with one tool left shows that tool on a plain button: no
// flyout arrow, no flyout, and a click or a right-click selects the tool. A second member back
// brings the group button back.
void ui_tool_palette_single_tool_group_is_a_plain_button() {
  CustomizationSettingsGuard settings_guard;
  auto&& [window, canvas] = ShownWindowFixture{};
  auto* palette = tool_palette(window);
  if (palette == nullptr) {
    return;
  }
  window.resize(window.width(), 1400);
  settle();
  auto* group = button_named(window, "moveToolButton");
  auto* member = button_named(window, "moveMemberButton");
  auto* artboard_member = button_named(window, "artboardMemberButton");
  auto* move = require_action(window, "toolMoveAction");
  auto* brush = require_action(window, "toolBrushAction");
  CHECK(on_palette(*palette, group));
  CHECK(!on_palette(*palette, member));
  CHECK(!on_palette(*palette, artboard_member));
  const auto group_rect = geometry_in_palette(*palette, *group);

  ToolPaletteCustomization customization;
  customization.hidden_tools = {QStringLiteral("tools.artboard")};
  window.apply_tool_palette_customization(customization);
  settle();
  CHECK(!on_palette(*palette, group));
  CHECK(on_palette(*palette, member));
  CHECK(!on_palette(*palette, artboard_member));
  CHECK(geometry_in_palette(*palette, *member) == group_rect);
  CHECK(member->defaultAction() == move);
  CHECK(member->menu() == nullptr);
  CHECK(!member->property("toolFlyout").toBool());
  brush->trigger();
  settle();
  member->click();
  settle();
  CHECK(move->isChecked());
  brush->trigger();
  settle();
  const QPoint center = member->rect().center();
  send_mouse(*member, QEvent::MouseButtonPress, center, Qt::RightButton, Qt::RightButton);
  send_mouse(*member, QEvent::MouseButtonRelease, center, Qt::RightButton, Qt::NoButton);
  settle();
  CHECK(move->isChecked());
  CHECK(QApplication::activePopupWidget() == nullptr);
  // Its key still works.
  brush->trigger();
  QTest::keyClick(canvas, Qt::Key_V);
  QApplication::processEvents();
  CHECK(move->isChecked());

  window.apply_tool_palette_customization(ToolPaletteCustomization{});
  settle();
  CHECK(on_palette(*palette, group));
  CHECK(!on_palette(*palette, member));
}

// GitHub issue 499: "No tool groups" in Customize Tool Palette gives every tool a plain button
// of its own (hidden tools stay off), opens no flyout, is stored under
// toolPalette/ungroupTools, and turns the dialog's group opening choice off.
void ui_tool_palette_no_tool_groups_shows_every_tool() {
  CustomizationSettingsGuard settings_guard;
  auto&& [window, canvas] = ShownWindowFixture{};
  auto* palette = tool_palette(window);
  auto* layout = window.tool_palette_layout();
  auto* footer = window.findChild<patchy::ui::ToolPaletteFooter*>(QStringLiteral("toolPaletteFooter"));
  CHECK(layout != nullptr && footer != nullptr);
  if (palette == nullptr || layout == nullptr || footer == nullptr) {
    return;
  }
  window.resize(window.width(), 1400);
  settle();
  (void)capture_widget_artifact("ui_tool_palette_tool_groups_one_column", *palette);

  // The dialog's check box, OK applies it.
  bool ran = false;
  when_customize_dialog_opens([&](QDialog& dialog) {
    ran = true;
    auto* ungroup = dialog.findChild<QCheckBox*>(QStringLiteral("toolPaletteUngroupToolsCheck"));
    auto* flyout_open = dialog.findChild<QComboBox*>(QStringLiteral("toolPaletteFlyoutOpenCombo"));
    auto* preview = dialog.findChild<QWidget*>(QStringLiteral("toolPaletteCustomizePreview"));
    CHECK(ungroup != nullptr && flyout_open != nullptr && preview != nullptr);
    if (ungroup == nullptr || flyout_open == nullptr || preview == nullptr) {
      dialog.reject();
      return;
    }
    CHECK(!ungroup->isChecked());
    CHECK(flyout_open->isEnabled());
    const int grouped_rows = preview->property("toolRows").toInt();
    ungroup->setChecked(true);
    QApplication::processEvents();
    CHECK(!flyout_open->isEnabled());
    // Every tool has a cell of its own in the preview; it shows the rows that fit.
    CHECK(preview->property("toolRows").toInt() > grouped_rows);
    CHECK(preview->property("shownToolRows").toInt() < preview->property("toolRows").toInt());
    CHECK(preview->mapTo(&dialog, QPoint(0, preview->height())).y() <= dialog.height());
    (void)capture_widget_artifact("ui_tool_palette_customize_no_tool_groups", dialog);
    press_dialog_button(dialog, QDialogButtonBox::Ok);
  });
  window.show_tool_palette_customization(patchy::ui::ToolPaletteCustomizeTab::Tools);
  CHECK(ran);
  settle();
  CHECK(window.tool_palette_customization().ungroup_tools);
  CHECK(patchy::ui::app_settings().value(QStringLiteral("toolPalette/ungroupTools")).toBool());
  (void)capture_widget_artifact("ui_tool_palette_no_tool_groups_one_column", *palette);

  // Two columns hold every tool in the window; Ruler hidden stays off.
  ToolPaletteCustomization customization = window.tool_palette_customization();
  customization.two_columns = true;
  customization.hidden_tools = {QStringLiteral("tools.ruler")};
  window.apply_tool_palette_customization(customization);
  settle();
  const auto palette_buttons = palette->findChildren<QToolButton*>();
  const auto shown_for = [&](const QAction* action) {
    return std::any_of(palette_buttons.begin(), palette_buttons.end(), [&](QToolButton* button) {
      return on_palette(*palette, button) && button->defaultAction() == action;
    });
  };
  int tools = 0;
  for (const auto& slot : layout->entries()) {
    for (const auto* member : slot.members) {
      const bool hidden = member->property(patchy::ui::kToolPaletteToolIdProperty).toString() == QLatin1String("tools.ruler");
      CHECK(shown_for(member) == !hidden);
      ++tools;
    }
  }
  CHECK(tools > 40);
  // No tool button outside the footer opens a menu.
  for (auto* button : palette_buttons) {
    if (on_palette(*palette, button) && !footer->isAncestorOf(button) &&
        button->objectName() != QLatin1String("qt_toolbar_ext_button")) {
      CHECK(button->menu() == nullptr);
    }
  }
  (void)capture_widget_artifact("ui_tool_palette_no_tool_groups_two_columns", *palette);
  // The Shift+M cycle still walks the Marquee tools.
  require_action(window, "toolMarqueeAction")->trigger();
  require_action(window, "toolCycleMarqueeAction")->trigger();
  QApplication::processEvents();
  CHECK(require_action(window, "toolEllipticalMarqueeAction")->isChecked());
  {
    patchy::ui::MainWindow second;
    CHECK(second.tool_palette_customization().ungroup_tools);
  }
  window.apply_tool_palette_customization(ToolPaletteCustomization{});
  settle();
  CHECK(!patchy::ui::app_settings().contains(QStringLiteral("toolPalette/ungroupTools")));
  CHECK(on_palette(*palette, button_named(window, "marqueeToolButton")));
}

// GitHub issue 499: with tool groups set to open on click, a press on another tool while a
// flyout is open closes the flyout and acts there: a plain tool is selected, a group opens
// its own flyout. A press elsewhere only closes the flyout.
void ui_tool_flyout_click_mode_hands_the_click_on() {
  CustomizationSettingsGuard settings_guard;
  auto&& [window, canvas] = ShownWindowFixture{};
  window.activateWindow();
  window.resize(window.width(), 1400);
  settle();
  window.apply_tool_palette_preferences(patchy::ui::ToolbarIconSize::Normal, patchy::ui::ToolFlyoutOpenMode::Click);
  auto* marquee_button = button_named(window, "marqueeToolButton");
  auto* lasso_button = button_named(window, "lassoToolButton");
  auto* brush_button = button_named(window, "brushToolButton");
  auto* marquee = require_action(window, "toolMarqueeAction");
  auto* lasso = require_action(window, "toolLassoAction");
  auto* brush = require_action(window, "toolBrushAction");
  auto* hand = require_action(window, "toolHandAction");
  // Clicks the Marquee group, then presses at `global` while its flyout is open.
  const auto open_and_press_at = [&](QPoint global) {
    auto* menu = marquee_button->menu();
    bool pressed = false;
    const auto connection = QObject::connect(menu, &QMenu::aboutToShow, menu, [&] {
      QTimer::singleShot(0, menu, [&] {
        process_events_until([&] { return menu->isVisible(); }, 1000);
        send_mouse(*menu, QEvent::MouseButtonPress, menu->mapFromGlobal(global), Qt::LeftButton, Qt::LeftButton);
        pressed = true;
        if (menu->isVisible()) {
          menu->close();
        }
      });
    });
    const QPoint center = marquee_button->rect().center();
    send_mouse(*marquee_button, QEvent::MouseButtonPress, center, Qt::LeftButton, Qt::LeftButton);
    process_events_until([&] { return pressed && !menu->isVisible(); }, 2000);
    send_mouse(*marquee_button, QEvent::MouseButtonRelease, center, Qt::LeftButton, Qt::NoButton);
    QObject::disconnect(connection);
    CHECK(pressed);
  };

  // A plain tool: the Brush.
  hand->trigger();
  settle();
  open_and_press_at(brush_button->mapToGlobal(brush_button->rect().center()));
  settle();
  CHECK(brush->isChecked());
  CHECK(QApplication::activePopupWidget() == nullptr);

  // Another group: its flyout opens and selects the tool it shows.
  {
    auto* lasso_menu = lasso_button->menu();
    int lasso_shows = 0;
    const auto connection = QObject::connect(lasso_menu, &QMenu::aboutToShow, lasso_menu, [&] {
      ++lasso_shows;
      QTimer::singleShot(0, lasso_menu, [&] {
        process_events_until([&] { return lasso_menu->isVisible(); }, 1000);
        lasso_menu->close();
      });
    });
    open_and_press_at(lasso_button->mapToGlobal(lasso_button->rect().center()));
    process_events_until([&] { return lasso_shows > 0 && !lasso_menu->isVisible(); }, 2000);
    settle();
    CHECK(lasso_shows == 1);
    CHECK(lasso->isChecked());
    QObject::disconnect(connection);
  }

  // Off the palette (the canvas): the flyout only closes; the Marquee it selected stays.
  hand->trigger();
  settle();
  open_and_press_at(canvas->mapToGlobal(canvas->rect().center()));
  settle();
  CHECK(marquee->isChecked());
  CHECK(QApplication::activePopupWidget() == nullptr);
  window.apply_tool_palette_preferences(patchy::ui::ToolbarIconSize::Normal, patchy::ui::ToolFlyoutOpenMode::Hold);
}

// GitHub issue 499: in two columns the footer (color well, Quick Mask, screen mode) is
// centered under the two tool columns and sits right under the last tool row; one column
// keeps it at the bottom of the bar, at the column's left.
void ui_tool_palette_two_column_footer_sits_centered_under_the_tools() {
  CustomizationSettingsGuard settings_guard;
  auto&& [window, canvas] = ShownWindowFixture{};
  auto* palette = tool_palette(window);
  auto* footer = window.findChild<patchy::ui::ToolPaletteFooter*>(QStringLiteral("toolPaletteFooter"));
  CHECK(footer != nullptr);
  if (palette == nullptr || footer == nullptr) {
    return;
  }
  window.resize(window.width(), 1200);
  settle();
  const auto bar_bottom = [&] { return palette->height() - palette->layout()->contentsMargins().top(); };
  CHECK(footer->geometry().bottom() + 1 == bar_bottom());
  CHECK(footer->foreground()->x() == 4);
  const int one_column_quick_mask_x = footer->quick_mask_button()->x();

  ToolPaletteCustomization customization;
  customization.two_columns = true;
  window.apply_tool_palette_customization(customization);
  window.resize(window.width(), 1200);
  settle();
  const auto move_rect = geometry_in_palette(*palette, *button_named(window, "moveToolButton"));
  const auto marquee_rect = geometry_in_palette(*palette, *button_named(window, "marqueeToolButton"));
  const double tools_center = (move_rect.left() + marquee_rect.right() + 1) / 2.0;
  const auto foreground_rect = geometry_in_palette(*palette, *footer->foreground());
  const auto background_rect = geometry_in_palette(*palette, *footer->background());
  const double well_center = (foreground_rect.left() + background_rect.right() + 1) / 2.0;
  const auto quick_mask_rect = geometry_in_palette(*palette, *footer->quick_mask_button());
  const double quick_mask_center = (quick_mask_rect.left() + quick_mask_rect.right() + 1) / 2.0;
  std::printf("[INFO] tools center %.1f, well center %.1f, Quick Mask center %.1f\n", tools_center, well_center,
              quick_mask_center);
  CHECK(std::abs(well_center - tools_center) <= 1.0);
  CHECK(std::abs(quick_mask_center - tools_center) <= 1.5);
  if (auto* screen_mode = footer->screen_mode_button(); screen_mode != nullptr) {
    const auto mode_rect = geometry_in_palette(*palette, *screen_mode);
    CHECK(std::abs((mode_rect.left() + mode_rect.right() + 1) / 2.0 - tools_center) <= 1.5);
  }
  int tools_bottom = 0;
  for (auto* button : palette->findChildren<QToolButton*>()) {
    if (on_palette(*palette, button) && !footer->isAncestorOf(button) && button->defaultAction() != nullptr &&
        button->defaultAction()->property(patchy::ui::kToolPaletteToolIdProperty).isValid()) {
      tools_bottom = std::max(tools_bottom, geometry_in_palette(*palette, *button).bottom() + 1);
    }
  }
  const int gap = footer->geometry().top() - tools_bottom;
  std::printf("[INFO] footer starts %d px under the last tool row\n", gap);
  CHECK(gap >= 0 && gap <= 12);
  CHECK(footer->geometry().bottom() + 1 < bar_bottom());
  (void)capture_widget_artifact("ui_tool_palette_two_column_footer", *palette);

  window.apply_tool_palette_customization(ToolPaletteCustomization{});
  window.resize(window.width(), 1200);
  settle();
  CHECK(footer->geometry().bottom() + 1 == bar_bottom());
  CHECK(footer->foreground()->x() == 4);
  CHECK(footer->quick_mask_button()->x() == one_column_quick_mask_x);
}

// GitHub issue 499: shortcut keys start together, left aligned, in a flyout (Lasso's L and
// its cycle's Shift+L) and in Customize Tool Palette's list (Marquee's M and Healing's J).
void ui_tool_flyout_shortcut_keys_start_in_one_column() {
  CustomizationSettingsGuard settings_guard;
  auto&& [window, canvas] = ShownWindowFixture{};
  window.resize(window.width(), 1400);
  settle();
  auto* button = button_named(window, "lassoToolButton");
  auto* menu = button->menu();
  auto* lasso = require_action(window, "toolLassoAction");
  auto* cycle = require_action(window, "toolCycleLassoAction");
  CHECK(!lasso->shortcut().isEmpty() && !cycle->shortcut().isEmpty());
  menu->popup(button->mapToGlobal(QPoint(button->width() + 2, 0)));
  process_events_until([&] { return menu->isVisible(); }, 1000);
  settle();
  const QImage image = menu->grab().toImage();
  const double scale = static_cast<double>(image.width()) / menu->width();
  (void)capture_widget_artifact("ui_tool_flyout_shortcut_column", *menu);
  const auto key_left = [&](QAction* action) {
    const QRect row = menu->actionGeometry(action);
    const QRect band(qRound(row.left() * scale), qRound(row.top() * scale), qRound(row.width() * scale) - 1,
                     qRound(row.height() * scale));
    return key_ink_left(image, band, qRound(6 * scale));
  };
  const int lasso_key = key_left(lasso);
  const int cycle_key = key_left(cycle);
  std::printf("[INFO] flyout keys start at %d (L) and %d (Shift+L)\n", lasso_key, cycle_key);
  CHECK(lasso_key > 0 && cycle_key > 0);
  CHECK(std::abs(lasso_key - cycle_key) <= qRound(2 * scale));
  menu->close();
  settle();

  bool ran = false;
  when_customize_dialog_opens([&](QDialog& dialog) {
    ran = true;
    auto* tree = dialog.findChild<QTreeWidget*>(QStringLiteral("toolPaletteCustomizeTree"));
    CHECK(tree != nullptr);
    if (tree == nullptr) {
      dialog.reject();
      return;
    }
    settle();
    auto* viewport = tree->viewport();
    double list_scale = 1.0;
    const auto row_key_left = [&](const char* name) {
      auto* row = tree_row(*tree, QLatin1String(name));
      if (row == nullptr) {
        return -1;
      }
      // Taller rows (Linux fonts) put a row under the list's fold: scroll it in first.
      tree->scrollToItem(row);
      settle();
      const QImage list = viewport->grab().toImage();
      list_scale = static_cast<double>(list.width()) / viewport->width();
      const int column = qRound(tree->header()->sectionViewportPosition(1) * list_scale);
      const QRect band = tree->visualItemRect(row);
      return key_ink_left(list, QRect(column, qRound(band.top() * list_scale), list.width() - column,
                                      qRound(band.height() * list_scale)),
                          list.width());
    };
    const int marquee_key = row_key_left("Marquee Tools");
    const int healing_key = row_key_left("Healing Tools");
    std::printf("[INFO] list keys start at %d (M) and %d (J)\n", marquee_key, healing_key);
    CHECK(marquee_key > 0 && healing_key > 0);
    CHECK(std::abs(marquee_key - healing_key) <= qRound(2 * list_scale));
    (void)capture_widget_artifact("ui_tool_palette_customize_shortcut_column", *tree);
    dialog.reject();
  });
  window.show_tool_palette_customization(patchy::ui::ToolPaletteCustomizeTab::Tools);
  CHECK(ran);
}

// Rows of the preview's painted strip whose pixel one logical pixel in from the left edge is
// not the palette's background: only the torn gap reaches there (icons and separators start
// at the margin).
int preview_torn_rows(QWidget& preview) {
  const QImage image = preview.grab().toImage();
  const QColor background = patchy::ui::theme().tool_palette_bg;
  const double scale = static_cast<double>(image.width()) / preview.width();
  const int x = qRound(scale);
  const int strip = std::min(image.height(), qRound(preview.property("stripHeight").toInt() * scale));
  int rows = 0;
  for (int y = 0; y < strip; ++y) {
    if (image.pixelColor(x, y) != background) {
      ++rows;
    }
  }
  return rows;
}

bool preview_is_cut(const QWidget& preview) {
  return preview.property("shownToolRows").toInt() < preview.property("toolRows").toInt();
}

// GitHub issue 499 comment: the dialog is resizable and its preview shows the rows of tools
// that fit in its height, then the footer; only when rows are left out does a torn gap mark
// the cut. The dialog opens short, inside the screen, and remembers the size it is dragged to.
void ui_tool_palette_customize_preview_fits_its_height() {
  CustomizationSettingsGuard settings_guard;
  auto&& [window, canvas] = ShownWindowFixture{};
  if (tool_palette(window) == nullptr) {
    return;
  }
  const QScreen* screen = window.screen();
  CHECK(screen != nullptr);
  if (screen == nullptr) {
    return;
  }
  const QSize available = screen->availableGeometry().size();
  QSize dragged_size;
  bool ran = false;
  when_customize_dialog_opens([&](QDialog& dialog) {
    ran = true;
    auto* tree = dialog.findChild<QTreeWidget*>(QStringLiteral("toolPaletteCustomizeTree"));
    auto* preview = dialog.findChild<QWidget*>(QStringLiteral("toolPaletteCustomizePreview"));
    auto* ungroup = dialog.findChild<QCheckBox*>(QStringLiteral("toolPaletteUngroupToolsCheck"));
    auto* two_columns = dialog.findChild<QRadioButton*>(QStringLiteral("toolPaletteTwoColumnsRadio"));
    auto* grip = dialog.findChild<QWidget*>(QStringLiteral("toolPaletteCustomizeSizeGrip"));
    CHECK(tree != nullptr && preview != nullptr && ungroup != nullptr && two_columns != nullptr && grip != nullptr);
    if (tree == nullptr || preview == nullptr || ungroup == nullptr || two_columns == nullptr || grip == nullptr) {
      dialog.reject();
      return;
    }
    settle();
    CHECK(grip->isVisible());
    // It opens short and inside the screen; the default single column does not fit, so the
    // preview is cut, with the gap drawn, and its strip stays inside the preview.
    CHECK(dialog.height() <= 480);
    CHECK(dialog.width() <= available.width() && dialog.height() <= available.height());
    CHECK(dialog.minimumHeight() <= dialog.height());
    CHECK(preview_is_cut(*preview));
    CHECK(preview->property("shownToolRows").toInt() > 0);
    CHECK(preview->property("stripHeight").toInt() <= preview->height());
    CHECK(preview_torn_rows(*preview) > 0);
    CHECK(preview->mapTo(&dialog, QPoint(0, preview->height())).y() <= dialog.height());
    (void)capture_widget_artifact("ui_tool_palette_customize_preview_cut", dialog);
    {
      // The gap reads in Light too.
      ColorSchemeRestorer light(patchy::ui::ColorSchemePreference::Light);
      settle();
      CHECK(preview_torn_rows(*preview) > 0);
      (void)capture_widget_artifact("ui_tool_palette_customize_preview_cut_light", dialog);
    }
    settle();
    // No tool groups nearly doubles the rows; the dialog keeps its size.
    const QSize open_size = dialog.size();
    const int grouped_rows = preview->property("toolRows").toInt();
    ungroup->setChecked(true);
    settle();
    CHECK(preview->property("toolRows").toInt() > grouped_rows);
    CHECK(preview_is_cut(*preview));
    CHECK(dialog.size() == open_size);
    ungroup->setChecked(false);
    // Two columns, and the dialog dragged taller until the whole palette fits: no gap.
    two_columns->setChecked(true);
    settle();
    const int shown_before = preview->property("shownToolRows").toInt();
    dialog.resize(dialog.width() + 40, available.height() - 60);
    settle();
    CHECK(tree->height() > 200);
    CHECK(preview->property("shownToolRows").toInt() >= shown_before);
    CHECK(!preview_is_cut(*preview));
    CHECK(preview_torn_rows(*preview) == 0);
    CHECK(preview->property("stripHeight").toInt() <= preview->height());
    (void)capture_widget_artifact("ui_tool_palette_customize_preview_tall", dialog);
    // Back at the smallest size, a short layout (only the first two clusters) still fits.
    for (int index = 2; index < tree->topLevelItemCount(); ++index) {
      auto* header = tree->topLevelItem(index);
      for (int row = 0; row < header->childCount(); ++row) {
        header->child(row)->setCheckState(0, Qt::Unchecked);
      }
    }
    dialog.resize(dialog.minimumSize());
    settle();
    const int short_rows = preview->property("toolRows").toInt();
    CHECK(short_rows > 0);
    CHECK(!preview_is_cut(*preview));
    CHECK(preview_torn_rows(*preview) == 0);
    (void)capture_widget_artifact("ui_tool_palette_customize_preview_short", dialog);
    dialog.resize(dialog.width() + 30, dialog.height() + 50);
    settle();
    dragged_size = dialog.size();
    dialog.reject();
  });
  window.show_tool_palette_customization(patchy::ui::ToolPaletteCustomizeTab::Tools);
  CHECK(ran);
  settle();
  // The dragged size is stored and the next open uses it.
  CHECK(patchy::ui::app_settings().value(QStringLiteral("toolPalette/customizeDialogSize")).toSize() ==
        dragged_size);
  ran = false;
  when_customize_dialog_opens([&](QDialog& dialog) {
    ran = true;
    settle();
    CHECK(dialog.size() == dragged_size);
    dialog.reject();
  });
  window.show_tool_palette_customization(patchy::ui::ToolPaletteCustomizeTab::Tools);
  CHECK(ran);
}

// Gray-level difference between the strongest ink in `rect` (viewport coordinates) and the
// background at its right end: negative for dark text on a light list, positive for light
// text on a dark one.
int ink_against_background(QTreeWidget& tree, const QRect& rect) {
  auto* viewport = tree.viewport();
  const QImage image = viewport->grab().toImage();
  const double scale = static_cast<double>(image.width()) / viewport->width();
  const QRect pixels(qRound(rect.left() * scale), qRound(rect.top() * scale), qRound(rect.width() * scale),
                     qRound(rect.height() * scale));
  const QRect area = pixels.intersected(image.rect());
  const int background = qGray(image.pixel(area.right() - 1, area.center().y()));
  int strongest = 0;
  for (int y = area.top(); y <= area.bottom(); ++y) {
    for (int x = area.left(); x <= area.right(); ++x) {
      const int difference = qGray(image.pixel(x, y)) - background;
      if (std::abs(difference) > std::abs(strongest)) {
        strongest = difference;
      }
    }
  }
  return strongest;
}

// GitHub issue 499 comment: the cluster headings (SELECT, PAINT...) and the shortcut keys
// follow the color scheme the list is painted in, also when the scheme changes while the
// dialog is open: dark text in Light, light text in Dark.
void ui_tool_palette_customize_tree_text_follows_the_scheme() {
  CustomizationSettingsGuard settings_guard;
  ColorSchemeRestorer restore(patchy::ui::ColorSchemePreference::Dark);
  auto&& [window, canvas] = ShownWindowFixture{};
  if (tool_palette(window) == nullptr) {
    return;
  }
  for (const auto opened_in : {patchy::ui::ColorSchemePreference::Dark, patchy::ui::ColorSchemePreference::Light}) {
    ColorSchemeRestorer::apply(opened_in);
    bool ran = false;
    when_customize_dialog_opens([&](QDialog& dialog) {
      ran = true;
      auto* tree = dialog.findChild<QTreeWidget*>(QStringLiteral("toolPaletteCustomizeTree"));
      CHECK(tree != nullptr && tree->topLevelItemCount() > 1);
      if (tree == nullptr || tree->topLevelItemCount() < 2) {
        dialog.reject();
        return;
      }
      settle();
      auto* heading = tree->topLevelItem(0);
      auto* tool_row = heading->child(0);
      CHECK(tool_row != nullptr && !tool_row->text(1).isEmpty());
      if (tool_row == nullptr) {
        dialog.reject();
        return;
      }
      const auto check_scheme = [&](patchy::ui::ColorSchemePreference scheme, const char* label) {
        ColorSchemeRestorer::apply(scheme);
        settle();
        tree->scrollToTop();
        settle();
        // Past the indentation: the expand sign is drawn in the scheme's colors either way.
        const QRect heading_rect = tree->visualItemRect(heading).adjusted(tree->indentation(), 0, 0, 0);
        const QRect key_rect = tree->visualRect(tree->model()->index(0, 1, tree->model()->index(0, 0)));
        const int heading_ink = ink_against_background(*tree, heading_rect);
        const int key_ink = ink_against_background(*tree, key_rect);
        std::cout << "[INFO] " << label << " heading ink " << heading_ink << ", key ink " << key_ink << "\n";
        if (scheme == patchy::ui::ColorSchemePreference::Light) {
          CHECK(heading_ink < -80);
          CHECK(key_ink < -80);
        } else {
          CHECK(heading_ink > 80);
          CHECK(key_ink > 80);
        }
      };
      if (opened_in == patchy::ui::ColorSchemePreference::Dark) {
        check_scheme(patchy::ui::ColorSchemePreference::Dark, "dark");
        check_scheme(patchy::ui::ColorSchemePreference::Light, "dark then light");
        (void)capture_widget_artifact("ui_tool_palette_customize_tree_light_after_dark", dialog);
      } else {
        check_scheme(patchy::ui::ColorSchemePreference::Light, "light");
        check_scheme(patchy::ui::ColorSchemePreference::Dark, "light then dark");
        (void)capture_widget_artifact("ui_tool_palette_customize_tree_dark_after_light", dialog);
      }
      dialog.reject();
    });
    window.show_tool_palette_customization(patchy::ui::ToolPaletteCustomizeTab::Tools);
    CHECK(ran);
    settle();
  }
}

// GitHub issue 537: every combo in the dialog (Toolbar icon size and Open tool groups on in
// the Tools tab, Preset and Icon size in the Layers Panel tab) is at least 90 px wide and as
// wide as its entries, and its open list shows every entry whole (the list was as wide as the
// box: "Adobe ...toshop", "Sm...st"). Checked in English, German and Russian, under the suite's
// style (Fusion: a menu) and the Windows styles (a list view) when Qt finds them (set
// QT_PLUGIN_PATH to the Qt kit's plugins folder, as for ui_preferences_combo_lists_fit_their_entries).
// Captures show the Layers Panel tab and the open lists. GitHub issue 538: the tab is "Layers Panel".
void ui_tool_palette_customize_combos_show_their_entries() {
  CustomizationSettingsGuard settings_guard;
  constexpr int kMinimumComboWidth = 90;
  const auto expect = [](bool ok, const std::string& message) {
    if (!ok) {
      patchy::test::report_failure(message);
    }
  };
  // The dialog with the open list painted where it sits, cropped around the box and the list.
  const auto capture_open_list = [](const std::string& name, QDialog& dialog, QComboBox& combo, QWidget& popup) {
    auto shot = dialog.grab().toImage();
    const auto origin = dialog.mapToGlobal(QPoint(0, 0));
    const auto popup_position = popup.mapToGlobal(QPoint(0, 0)) - origin;
    {
      QPainter painter(&shot);
      painter.drawPixmap(popup_position, popup.grab());
    }
    const auto crop = QRect(combo.mapToGlobal(QPoint(0, 0)) - origin, combo.size())
                          .united(QRect(popup_position, popup.size()))
                          .adjusted(-120, -14, 48, 14)
                          .intersected(shot.rect());
    save_image_artifact(name, shot.copy(crop));
  };
  const std::map<QString, std::string> captured_combos = {
      {QStringLiteral("layerFooterPresetCombo"), "preset"},
      {QStringLiteral("layerFooterIconSizeCombo"), "layers_icon_size"},
      {QStringLiteral("toolPaletteIconSizeCombo"), "tools_icon_size"},
      {QStringLiteral("toolPaletteFlyoutOpenCombo"), "tool_groups"},
  };
  QStringList style_keys{QString()};  // the suite's own style first
  for (const auto& key : {QStringLiteral("windows11"), QStringLiteral("windowsvista")}) {
    if (QStyleFactory::keys().contains(key, Qt::CaseInsensitive)) {
      style_keys.append(key);
    }
  }
  auto& manager = patchy::ui::LocalizationManager::instance();
  int checked = 0;
  for (const auto& style_key : style_keys) {
    if (!style_key.isEmpty()) {
      QApplication::setStyle(new patchy::ui::AppStyle(QStyleFactory::create(style_key)));
    }
    const auto style_name = style_key.isEmpty() ? std::string("suite") : style_key.toStdString();
    {
      auto&& [window, canvas] = ShownWindowFixture{};
      for (const auto& code : {QStringLiteral("en"), QStringLiteral("de"), QStringLiteral("ru")}) {
        CHECK(manager.set_language(code, false));
        QApplication::processEvents();
        const auto lang = code.toStdString();
        bool ran = false;
        when_customize_dialog_opens([&](QDialog& dialog) {
          ran = true;
          auto* tabs = dialog.findChild<QTabWidget*>(QStringLiteral("toolPaletteCustomizeTabs"));
          CHECK(tabs != nullptr && tabs->count() == 2);
          if (tabs == nullptr || tabs->count() != 2) {
            dialog.reject();
            return;
          }
          for (int tab = 0; tab < tabs->count(); ++tab) {
            tabs->setCurrentIndex(tab);
            settle();
            auto* page = tabs->widget(tab);
            if (tab == 1 && style_key.isEmpty() && (code == QStringLiteral("en") || code == QStringLiteral("de"))) {
              (void)capture_widget_artifact("ui_tool_palette_customize_layers_panel_tab_" + lang, dialog);
            }
            for (auto* combo : page->findChildren<QComboBox*>()) {
              if (!combo->isVisibleTo(page) || combo->count() == 0) {
                continue;
              }
              ++checked;
              const auto where = style_name + " / " + lang + " / " + combo->objectName().toStdString() + ": ";
              expect(combo->width() >= kMinimumComboWidth,
                     where + "the box is " + std::to_string(combo->width()) + " px wide");
              expect(combo->width() >= combo->minimumSizeHint().width(),
                     where + "the box is squeezed: " + std::to_string(combo->width()) + " px, needs " +
                         std::to_string(combo->minimumSizeHint().width()));
              combo->showPopup();
              settle();
              auto* view = combo->view();
              auto* popup = view->window();
              expect(popup->isVisible(), where + "the list did not open");
              const int room = view->viewport()->width();
              for (int item = 0; item < combo->count(); ++item) {
                const auto item_text = combo->itemText(item);
                if (item_text.isEmpty()) {
                  continue;  // a separator
                }
                const auto index = view->model()->index(item, combo->modelColumn(), view->rootIndex());
                // As each style draws the row at its real width (ui_preferences_combo_lists_fit_their_entries).
                const int text = std::max(combo->fontMetrics().horizontalAdvance(item_text),
                                          view->fontMetrics().horizontalAdvance(item_text));
                auto* delegate = view->itemDelegateForIndex(index);
                QStyleOptionViewItem option;
                option.initFrom(view);
                option.font = view->font();
                int needs = 0;
                int has = room;
                if (qobject_cast<QStyledItemDelegate*>(delegate) != nullptr) {
                  option.widget = view;
                  option.text = item_text;
                  option.features |= QStyleOptionViewItem::HasDisplay;
                  option.rect = view->visualRect(index);
                  needs = text;
                  has = view->style()->subElementRect(QStyle::SE_ItemViewItemText, &option, view).width() -
                        2 * (view->style()->pixelMetric(QStyle::PM_FocusFrameHMargin, nullptr, view) + 1);
                } else {
                  option.rect = QRect(0, 0, text, combo->fontMetrics().height());
                  needs = delegate->sizeHint(option, index).width();
                }
                expect(needs <= has && text < room, where + "'" + item_text.toStdString() + "' needs " +
                                                        std::to_string(needs) + " px of " + std::to_string(has) +
                                                        " (text " + std::to_string(text) + ", list " +
                                                        std::to_string(room) + ")");
              }
              if (const auto found = captured_combos.find(combo->objectName());
                  found != captured_combos.end() && (code == QStringLiteral("en") || code == QStringLiteral("de"))) {
                capture_open_list("ui_tool_palette_customize_combo_list_" + found->second + "_" + lang + "_" +
                                      style_name,
                                  dialog, *combo, *popup);
              }
              combo->hidePopup();
              settle();
            }
          }
          expect(code != QStringLiteral("en") || tabs->tabText(1) == QStringLiteral("Layers Panel"),
                 "the second tab is '" + tabs->tabText(1).toStdString() + "'");
          dialog.reject();
        });
        window.show_tool_palette_customization(patchy::ui::ToolPaletteCustomizeTab::LayerButtons);
        CHECK(ran);
        settle();
      }
      CHECK(manager.set_language(QStringLiteral("en"), false));
    }
    if (!style_key.isEmpty()) {
      QApplication::setStyle(new patchy::ui::AppStyle);
    }
  }
  CHECK(checked >= 4);
}

}  // namespace

std::vector<patchy::test::TestCase> tool_palette_customize_tests() {
  return {
      {"ui_tool_palette_customize_hidden_tools_keep_hotkeys", ui_tool_palette_customize_hidden_tools_keep_hotkeys},
      {"ui_tool_palette_two_columns_place_tools_side_by_side",
       ui_tool_palette_two_columns_place_tools_side_by_side},
      {"ui_tool_palette_wheel_scrolls_overflowing_column", ui_tool_palette_wheel_scrolls_overflowing_column},
      {"ui_tool_palette_scroll_mode_keeps_expand_button_reachable",
       ui_tool_palette_scroll_mode_keeps_expand_button_reachable},
      {"ui_layer_footer_buttons_hide_from_their_menu", ui_layer_footer_buttons_hide_from_their_menu},
      {"ui_layers_panel_gear_opens_layer_button_customize", ui_layers_panel_gear_opens_layer_button_customize},
      {"ui_tool_palette_customize_dialog_applies_cancels_and_resets",
       ui_tool_palette_customize_dialog_applies_cancels_and_resets},
      {"ui_script_tool_palette_reads_and_sets", ui_script_tool_palette_reads_and_sets},
      {"ui_tool_palette_settings_from_an_older_version_show_new_tools",
       ui_tool_palette_settings_from_an_older_version_show_new_tools},
      {"ui_tool_palette_dialog_lists_every_palette_tool", ui_tool_palette_dialog_lists_every_palette_tool},
      {"ui_preferences_opens_customize_tool_palette", ui_preferences_opens_customize_tool_palette},
      {"ui_tool_palette_footer_items_hide_and_preview", ui_tool_palette_footer_items_hide_and_preview},
      {"ui_tool_flyout_open_modes_click_and_hover", ui_tool_flyout_open_modes_click_and_hover},
      {"ui_layer_footer_optional_buttons_start_hidden", ui_layer_footer_optional_buttons_start_hidden},
      {"ui_layer_footer_move_and_merge_buttons_follow_modifiers",
       ui_layer_footer_move_and_merge_buttons_follow_modifiers},
      {"ui_layer_footer_merge_visible_and_rasterize_buttons", ui_layer_footer_merge_visible_and_rasterize_buttons},
      {"ui_layer_footer_rasterize_button_opens_its_menu", ui_layer_footer_rasterize_button_opens_its_menu},
      {"ui_layer_footer_order_resolves_unknown_and_missing_ids", ui_layer_footer_order_resolves_unknown_and_missing_ids},
      {"ui_layer_footer_follows_stored_order", ui_layer_footer_follows_stored_order},
      {"ui_tool_palette_dialog_reorders_layer_buttons", ui_tool_palette_dialog_reorders_layer_buttons},
      {"ui_layer_footer_presets_set_order_and_shown_buttons", ui_layer_footer_presets_set_order_and_shown_buttons},
      {"ui_tool_palette_dialog_layer_button_presets", ui_tool_palette_dialog_layer_button_presets},
      {"ui_layer_footer_wraps_one_button_at_a_time", ui_layer_footer_wraps_one_button_at_a_time},
      {"ui_layer_footer_icon_size_persists_and_applies", ui_layer_footer_icon_size_persists_and_applies},
      {"ui_layer_footer_arrange_icons_draw_in_both_schemes", ui_layer_footer_arrange_icons_draw_in_both_schemes},
      {"ui_tool_palette_single_tool_group_is_a_plain_button", ui_tool_palette_single_tool_group_is_a_plain_button},
      {"ui_tool_palette_no_tool_groups_shows_every_tool", ui_tool_palette_no_tool_groups_shows_every_tool},
      {"ui_tool_flyout_click_mode_hands_the_click_on", ui_tool_flyout_click_mode_hands_the_click_on},
      {"ui_tool_palette_two_column_footer_sits_centered_under_the_tools",
       ui_tool_palette_two_column_footer_sits_centered_under_the_tools},
      {"ui_tool_flyout_shortcut_keys_start_in_one_column", ui_tool_flyout_shortcut_keys_start_in_one_column},
      {"ui_tool_palette_customize_preview_fits_its_height", ui_tool_palette_customize_preview_fits_its_height},
      {"ui_tool_palette_customize_combos_show_their_entries", ui_tool_palette_customize_combos_show_their_entries},
      {"ui_tool_palette_customize_tree_text_follows_the_scheme",
       ui_tool_palette_customize_tree_text_follows_the_scheme},
  };
}
