// The right column's Color panel (GitHub issue 365; docs/right-docks.md, "Color panel"):
// a click in its field or hue strip sets the foreground, Alt at the press sets the
// background for the whole drag, the panel follows color changes made elsewhere, the
// Window menu and patchy.ui reach it, and a saved layout from before it keeps its panels.
#include "ui/app_settings.hpp"
#include "ui/canvas_widget.hpp"
#include "ui/color_dock_panel.hpp"
#include "ui/color_field_widgets.hpp"
#include "ui/color_panel.hpp"
#include "ui/main_window.hpp"
#include "ui/panel_column.hpp"
#include "ui/tool_palette_footer.hpp"
#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QColor>
#include <QDataStream>
#include <QDialog>
#include <QLabel>
#include <QMenu>
#include <QSplitter>
#include <QStatusBar>
#include <QWidget>
#include <memory>
#include <vector>
#include "scripting_test_support.hpp"
#include "test_harness.hpp"
#include "ui_test_access.hpp"
#include "ui_test_groups.hpp"
#include "ui_test_support.hpp"

namespace {

using namespace patchy::test::ui;
using namespace patchy::test::ui::scripting_support;
using patchy::ui::ColorDockPanel;
using patchy::ui::MainWindowTestAccess;

const QString kColorPanel = QStringLiteral("colorDock");

ColorDockPanel* open_color_panel(patchy::ui::MainWindow& window) {
  MainWindowTestAccess::show_panel(window, kColorPanel);
  QApplication::processEvents();
  QApplication::processEvents();
  auto* panel = window.findChild<ColorDockPanel*>(QStringLiteral("colorPanel"));
  CHECK(panel != nullptr);
  if (panel != nullptr) {
    CHECK(panel->field()->isVisible());
    CHECK(panel->field()->width() >= 40 && panel->field()->height() >= 40);
    CHECK(panel->hue_strip()->isVisible());
  }
  return panel;
}

// The color the field maps a point to, the picker window's mapping: saturation across,
// value down, both snapping to the ends within 2 px.
QColor field_color_at(int hue, QPoint point, QSize size) {
  const auto max_x = std::max(1, size.width() - 1);
  const auto max_y = std::max(1, size.height() - 1);
  const int saturation = point.x() <= 2 ? 0 : (point.x() >= max_x - 2 ? 255 : patchy::ui::rounded_scaled_channel(point.x(), max_x, 255));
  const int value =
      point.y() <= 2 ? 255 : (point.y() >= max_y - 2 ? 0 : 255 - patchy::ui::rounded_scaled_channel(point.y(), max_y, 255));
  return QColor::fromHsv(hue, saturation, value);
}

int strip_hue_at(int y, int height) {
  return patchy::ui::rounded_scaled_channel(y, std::max(1, height - 1), 359);
}

void click(QWidget& widget, QPoint point, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
  send_mouse(widget, QEvent::MouseButtonPress, point, Qt::LeftButton, Qt::LeftButton, modifiers);
  send_mouse(widget, QEvent::MouseButtonRelease, point, Qt::LeftButton, Qt::NoButton, modifiers);
  QApplication::processEvents();
}

void ui_color_panel_click_sets_foreground_and_alt_sets_background() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* panel = open_color_panel(window);
  if (panel == nullptr) {
    return;
  }
  auto* field = panel->field();
  auto* strip = panel->hue_strip();
  const auto background_before = canvas->secondary_color();

  // A plain click sets the foreground to the color under the pointer; the background stays.
  const QPoint first(field->width() * 3 / 4, field->height() / 3);
  const auto first_expected = field_color_at(panel->hue(), first, field->size());
  click(*field, first);
  CHECK(canvas->primary_color().rgb() == first_expected.rgb());
  CHECK(canvas->secondary_color().rgb() == background_before.rgb());
  CHECK(panel->foreground_chip()->color().rgb() == first_expected.rgb());
  CHECK(window.statusBar()->currentMessage() == QStringLiteral("Foreground color changed"));

  // Alt at the press sets the background, and the foreground stays put.
  const auto hue = panel->hue();
  const QPoint second(field->width() / 4, field->height() / 2);
  const auto second_expected = field_color_at(hue, second, field->size());
  send_mouse(*field, QEvent::MouseButtonPress, second, Qt::LeftButton, Qt::LeftButton, Qt::AltModifier);
  QApplication::processEvents();
  CHECK(canvas->secondary_color().rgb() == second_expected.rgb());
  CHECK(canvas->primary_color().rgb() == first_expected.rgb());
  CHECK(panel->background_chip()->color().rgb() == second_expected.rgb());
  CHECK(window.statusBar()->currentMessage() == QStringLiteral("Background color changed"));
  // Alt released mid-drag: the drag keeps setting the background, as the Pick tool's does.
  const QPoint third(field->width() / 2, field->height() * 3 / 4);
  const auto third_expected = field_color_at(hue, third, field->size());
  send_mouse(*field, QEvent::MouseMove, third, Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
  QApplication::processEvents();
  CHECK(canvas->secondary_color().rgb() == third_expected.rgb());
  CHECK(canvas->primary_color().rgb() == first_expected.rgb());
  send_mouse(*field, QEvent::MouseButtonRelease, third, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
  QApplication::processEvents();
  // The field still shows the foreground.
  CHECK(panel->hue() == hue);
  CHECK(field_color_at(panel->hue(), first, field->size()).rgb() ==
        QColor::fromHsv(panel->hue(), panel->saturation(), panel->value()).rgb());

  // The hue strip: Alt-click gives the background that hue at full strength...
  const int alt_y = strip->height() / 3;
  click(*strip, QPoint(strip->width() / 2, alt_y), Qt::AltModifier);
  CHECK(canvas->secondary_color().rgb() == QColor::fromHsv(strip_hue_at(alt_y, strip->height()), 255, 255).rgb());
  CHECK(canvas->primary_color().rgb() == first_expected.rgb());
  // ...and a plain click turns the foreground's hue, keeping its saturation and value.
  const auto saturation = panel->saturation();
  const auto value = panel->value();
  const int plain_y = strip->height() * 2 / 3;
  const auto background_after_alt = canvas->secondary_color();
  click(*strip, QPoint(strip->width() / 2, plain_y));
  const auto turned = QColor::fromHsv(strip_hue_at(plain_y, strip->height()), saturation, value);
  CHECK(canvas->primary_color().rgb() == turned.rgb());
  CHECK(canvas->secondary_color().rgb() == background_after_alt.rgb());
  CHECK(panel->hue() == strip_hue_at(plain_y, strip->height()));
  // The new plain press is a foreground pick again, even right after an Alt drag.
  const QPoint fourth(field->width() / 3, field->height() / 4);
  const auto fourth_expected = field_color_at(panel->hue(), fourth, field->size());
  click(*field, fourth);
  CHECK(canvas->primary_color().rgb() == fourth_expected.rgb());
  CHECK(canvas->secondary_color().rgb() == background_after_alt.rgb());

  if (auto* column = MainWindowTestAccess::panel_column(window); column != nullptr) {
    save_widget_artifact("ui_color_panel_in_column", *column);
  }
  save_widget_artifact("ui_color_panel", *MainWindowTestAccess::panel_section(window, kColorPanel));
}

void ui_color_panel_follows_color_changes_elsewhere() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* panel = open_color_panel(window);
  if (panel == nullptr) {
    return;
  }
  const auto check_pair = [&] {
    CHECK(panel->foreground_chip()->color().rgb() == canvas->primary_color().rgb());
    CHECK(panel->background_chip()->color().rgb() == canvas->secondary_color().rgb());
  };
  check_pair();
  click(*panel->field(), QPoint(panel->field()->width() * 3 / 4, panel->field()->height() / 4));
  check_pair();

  // The tool palette's Swap and Default commands, which the panel's glyphs also trigger.
  const auto foreground = canvas->primary_color();
  const auto background = canvas->secondary_color();
  require_action(window, "colorSwapAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->primary_color().rgb() == background.rgb());
  CHECK(canvas->secondary_color().rgb() == foreground.rgb());
  check_pair();
  auto* swap_glyph = window.findChild<QAbstractButton*>(QStringLiteral("colorPanelSwapButton"));
  CHECK(swap_glyph != nullptr && swap_glyph->isVisible() && swap_glyph->isEnabled());
  if (swap_glyph == nullptr) {
    return;
  }
  swap_glyph->click();
  QApplication::processEvents();
  CHECK(canvas->primary_color().rgb() == foreground.rgb());
  check_pair();
  auto* default_glyph = window.findChild<QAbstractButton*>(QStringLiteral("colorPanelDefaultButton"));
  CHECK(default_glyph != nullptr && default_glyph->isVisible());
  if (default_glyph == nullptr) {
    return;
  }
  default_glyph->click();
  QApplication::processEvents();
  CHECK(canvas->primary_color().rgb() == QColor(Qt::black).rgb());
  CHECK(canvas->secondary_color().rgb() == QColor(Qt::white).rgb());
  check_pair();

  // The foreground chip opens the Foreground Color window; a pick there moves the
  // panel's chip and field.
  panel->foreground_chip()->click();
  QApplication::processEvents();
  auto* dialog = window.findChild<QDialog*>(QStringLiteral("patchyColorDialog"));
  CHECK(dialog != nullptr && dialog->isVisible());
  if (dialog == nullptr) {
    return;
  }
  CHECK(dialog->property("patchy.colorTarget").toString() == QStringLiteral("foreground"));
  auto* hue_slider = dialog->findChild<QWidget*>(QStringLiteral("patchyHueSlider"));
  auto* plane = dialog->findChild<QWidget*>(QStringLiteral("patchyColorPlane"));
  CHECK(hue_slider != nullptr && plane != nullptr);
  if (hue_slider == nullptr || plane == nullptr) {
    return;
  }
  click(*plane, QPoint(plane->width() - 1, 0));  // full saturation and value
  click(*hue_slider, QPoint(4, hue_slider->height() * 2 / 3));
  const auto picked = canvas->primary_color();
  CHECK(picked.hsvSaturation() == 255 && picked.value() == 255);
  check_pair();
  CHECK(panel->hue() == picked.hsvHue());
  CHECK(panel->saturation() == 255 && panel->value() == 255);

  // A pick in the panel moves the open window's color with it.
  click(*panel->field(), QPoint(panel->field()->width() / 2, panel->field()->height() / 2));
  auto* picker = dialog->findChild<patchy::ui::PatchyColorPicker*>(QStringLiteral("patchyAdvancedColorPicker"));
  CHECK(picker != nullptr);
  if (picker != nullptr) {
    CHECK(picker->currentColor().rgb() == canvas->primary_color().rgb());
  }
  // An Alt pick sets the background and leaves the Foreground window alone.
  const auto window_color = picker != nullptr ? picker->currentColor() : QColor();
  click(*panel->field(), QPoint(4, panel->field()->height() - 6), Qt::AltModifier);
  CHECK(canvas->secondary_color().rgb() != QColor(Qt::white).rgb());
  if (picker != nullptr) {
    CHECK(picker->currentColor().rgb() == window_color.rgb());
  }
  check_pair();
  dialog->close();
  QApplication::processEvents();
}

void ui_color_panel_window_menu_and_script_name() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* column = MainWindowTestAccess::panel_column(window);
  CHECK(column != nullptr && column->has_panel(kColorPanel));
  if (column == nullptr) {
    return;
  }
  // First launch: open and collapsed, just above Palette.
  CHECK(column->panel_visible(kColorPanel));
  CHECK(!column->panel_expanded(kColorPanel));
  const auto order = column->section_order();
  CHECK(order.indexOf(kColorPanel) + 1 == order.indexOf(QStringLiteral("paletteDock")));
  CHECK(column->title_label(kColorPanel)->text() == QStringLiteral("Color"));

  auto* entry = require_action(window, "windowPanelColorAction");
  CHECK(entry->text().remove(QLatin1Char('&')) == QStringLiteral("Color"));
  CHECK(entry->menuRole() == QAction::NoRole);
  // Like every panel entry: an open panel in front closes, a closed one opens expanded.
  entry->trigger();
  QApplication::processEvents();
  CHECK(!column->panel_visible(kColorPanel));
  CHECK(!entry->isChecked());
  entry->trigger();
  QApplication::processEvents();
  CHECK(column->panel_visible(kColorPanel));
  CHECK(column->panel_expanded(kColorPanel));
  CHECK(entry->isChecked());

  CHECK(run_script(window, QStringLiteral(R"JS(
    patchy.ui.closePanel('color');
    var before = patchy.ui.panelState('color');
    console.log('before=' + before.open);
    patchy.ui.expandPanel('color');
    var after = patchy.ui.panelState('color');
    console.log('after=' + after.open + ',' + after.expanded);
  )JS")));
  CHECK(backlog_contains(window, QStringLiteral("before=false")));
  CHECK(backlog_contains(window, QStringLiteral("after=true,true")));
  CHECK(column->panel_visible(kColorPanel));
  CHECK(column->panel_expanded(kColorPanel));
}

// A layout saved by a version without the Color panel (layout version 2) keeps every
// choice it records; the Color panel joins it collapsed, above Palette.
void ui_panel_layout_from_before_color_panel_keeps_its_panels() {
  const QStringList keys = {QStringLiteral("window/panelLayoutVersion"), QStringLiteral("window/hiddenPanels"),
                            QStringLiteral("window/collapsedPanels"), QStringLiteral("window/frontPanel"),
                            QStringLiteral("window/panelColumnWidth"), QStringLiteral("window/panelSplitterState"),
                            QStringLiteral("window/panelOrder")};
  std::vector<std::unique_ptr<SettingsValueRestorer>> restorers;
  for (const auto& key : keys) {
    restorers.push_back(std::make_unique<SettingsValueRestorer>(key));
  }
  patchy::ui::MainWindow window;
  auto* column = MainWindowTestAccess::panel_column(window);
  CHECK(column != nullptr);
  if (column == nullptr) {
    return;
  }
  // The heights of the five sections a version 2 column had, as its splitter saved them.
  QByteArray old_heights;
  {
    QSplitter splitter(Qt::Vertical);
    splitter.setChildrenCollapsible(false);
    splitter.setHandleWidth(column->splitter()->handleWidth());
    for (int index = 0; index < 5; ++index) {
      splitter.addWidget(new QWidget(&splitter));
    }
    splitter.resize(300, 700);
    splitter.setSizes({400, 24, 160, 24, 24});
    old_heights = splitter.saveState();
  }
  {
    auto settings = patchy::ui::app_settings();
    for (const auto& key : keys) {
      settings.remove(key);
    }
    settings.setValue(QStringLiteral("window/panelLayoutVersion"), 2);
    settings.setValue(QStringLiteral("window/hiddenPanels"), QStringList{QStringLiteral("historyDock")});
    settings.setValue(QStringLiteral("window/collapsedPanels"),
                      QStringList{QStringLiteral("historyDock"), QStringLiteral("propertiesDock"),
                                  QStringLiteral("paletteDock")});
    settings.setValue(QStringLiteral("window/frontPanel"), QStringLiteral("channelsDock"));
    settings.setValue(QStringLiteral("window/panelOrder"),
                      QStringList{QStringLiteral("layersDock"), QStringLiteral("historyDock"),
                                  QStringLiteral("infoDock"), QStringLiteral("propertiesDock"),
                                  QStringLiteral("paletteDock")});
    settings.setValue(QStringLiteral("window/panelSplitterState"), old_heights);
    settings.sync();
  }
  // Read before the first show, as the constructor does on a desktop.
  MainWindowTestAccess::read_panel_layout(window);
  show_window(window);
  CHECK(column->section_order() == (QStringList{QStringLiteral("layersDock"), QStringLiteral("historyDock"),
                                                QStringLiteral("infoDock"), QStringLiteral("propertiesDock"),
                                                kColorPanel, QStringLiteral("paletteDock")}));
  CHECK(!column->panel_visible(QStringLiteral("historyDock")));
  CHECK(column->panel_in_front(QStringLiteral("channelsDock")));
  CHECK(column->panel_expanded(QStringLiteral("infoDock")));
  CHECK(!column->panel_expanded(QStringLiteral("propertiesDock")));
  CHECK(column->panel_visible(kColorPanel));
  CHECK(!column->panel_expanded(kColorPanel));
  // Collapsed, the new section is exactly its title row, like the other collapsed ones.
  CHECK(column->section_widget(kColorPanel)->height() ==
        column->section_widget(QStringLiteral("paletteDock"))->height());
  CHECK(column->section_widget(kColorPanel)->isVisible());
  // Saving writes the current version, which a later launch reads as is.
  MainWindowTestAccess::write_panel_layout(window);
  CHECK(patchy::ui::app_settings().value(QStringLiteral("window/panelLayoutVersion")).toInt() == 3);
  CHECK(patchy::ui::app_settings().value(QStringLiteral("window/panelOrder")).toStringList().contains(kColorPanel));
}

}  // namespace

std::vector<patchy::test::TestCase> color_dock_panel_tests() {
  return {
      {"ui_color_panel_click_sets_foreground_and_alt_sets_background",
       ui_color_panel_click_sets_foreground_and_alt_sets_background},
      {"ui_color_panel_follows_color_changes_elsewhere", ui_color_panel_follows_color_changes_elsewhere},
      {"ui_color_panel_window_menu_and_script_name", ui_color_panel_window_menu_and_script_name},
      {"ui_panel_layout_from_before_color_panel_keeps_its_panels",
       ui_panel_layout_from_before_color_panel_keeps_its_panels},
  };
}
