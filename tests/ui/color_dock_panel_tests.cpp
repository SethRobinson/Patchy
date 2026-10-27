// The right column's Color panel (GitHub issue 365; docs/right-docks.md, "Color panel"):
// a click in its field or hue strip sets the foreground, Alt at the press sets the
// background for the whole drag, the panel follows color changes made elsewhere, the
// Window menu and patchy.ui reach it, and a saved layout from before it keeps its panels.
// Alt over the field, the hue strip or a Palette swatch shows the background cursor, and an
// Alt-click on a Palette swatch sets the background. The view menu switches to RGB or HSB
// sliders, which set the foreground, follow it, and are remembered (GitHub issue 404), and
// to CMYK sliders in the working CMYK space (the issue's follow-up). A drag in the field
// keeps its per-move work small: no gradient re-render, no settings write until the
// release (GitHub issue 464).
#include "color/color_management.hpp"
#include "core/document.hpp"
#include "core/palette_presets.hpp"
#include "ui/app_settings.hpp"
#include "ui/canvas_widget.hpp"
#include "ui/color_dock_panel.hpp"
#include "ui/color_field_widgets.hpp"
#include "ui/color_panel.hpp"
#include "ui/main_window.hpp"
#include "ui/panel_column.hpp"
#include "ui/tool_cursors.hpp"
#include "ui/tool_palette_footer.hpp"
#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QColor>
#include <QContextMenuEvent>
#include <QDataStream>
#include <QDialog>
#include <QEnterEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QSpinBox>
#include <QSplitter>
#include <QTabBar>
#include <QTimer>
#include <QToolButton>
#include <QStatusBar>
#include <QWidget>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
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
  // First launch: open, expanded and in front of Palette in a tab group at the top.
  CHECK(column->panel_visible(kColorPanel));
  CHECK(column->panel_expanded(kColorPanel));
  CHECK(column->section_order().first() == kColorPanel);
  CHECK(column->section_panel_ids(kColorPanel) == (QStringList{kColorPanel, QStringLiteral("paletteDock")}));
  CHECK(column->panel_in_front(kColorPanel));
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
                            QStringLiteral("window/panelOrder"), QStringLiteral("window/panelGroups")};
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
  // The record has no tab order: the group keeps the default one.
  CHECK(column->section_panel_ids(QStringLiteral("layersDock")) ==
        (QStringList{QStringLiteral("layersDock"), QStringLiteral("channelsDock"), QStringLiteral("pathsDock")}));
  // Saving writes the current version, which a later launch reads as is.
  MainWindowTestAccess::write_panel_layout(window);
  CHECK(patchy::ui::app_settings().value(QStringLiteral("window/panelLayoutVersion")).toInt() == 4);
  CHECK(patchy::ui::app_settings().value(QStringLiteral("window/panelGroups")).toStringList() ==
        QStringList{QStringLiteral("layersDock,channelsDock,pathsDock")});
  CHECK(patchy::ui::app_settings().value(QStringLiteral("window/panelOrder")).toStringList().contains(kColorPanel));
}

// The background cursor (the Pick tool's, eyedropper_target_cursor(true)) over a widget.
bool shows_background_cursor(const QWidget& widget) {
  const auto expected = patchy::ui::eyedropper_target_cursor(true);
  return widget.cursor().shape() == Qt::BitmapCursor && widget.cursor().hotSpot() == expected.hotSpot() &&
         widget.cursor().pixmap().cacheKey() == expected.pixmap().cacheKey();
}

void send_key(QWidget& receiver, QEvent::Type type, int key, Qt::KeyboardModifiers modifiers) {
  QKeyEvent event(type, key, modifiers);
  QApplication::sendEvent(&receiver, &event);
  QApplication::processEvents();
}

void hover(QWidget& widget, QPoint point, Qt::KeyboardModifiers modifiers) {
  QEnterEvent enter(QPointF(point), QPointF(widget.mapTo(widget.window(), point)),
                    QPointF(widget.mapToGlobal(point)));
  QApplication::sendEvent(&widget, &enter);
  send_mouse(widget, QEvent::MouseMove, point, Qt::NoButton, Qt::NoButton, modifiers);
  QApplication::processEvents();
}

void ui_color_panel_alt_shows_background_cursor() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* panel = open_color_panel(window);
  if (panel == nullptr) {
    return;
  }
  auto* field = panel->field();
  auto* strip = panel->hue_strip();
  const auto field_shape = field->cursor().shape();
  const auto strip_shape = strip->cursor().shape();
  CHECK(field_shape == Qt::CrossCursor);
  CHECK(strip_shape == Qt::PointingHandCursor);
  const QPoint middle(field->width() / 2, field->height() / 2);

  // Alt held on arrival, then released and pressed again over the field: the cursor
  // follows the key itself (sent to the window, which has the focus, not to the field).
  hover(*field, middle, Qt::AltModifier);
  CHECK(shows_background_cursor(*field));
  send_key(window, QEvent::KeyRelease, Qt::Key_Alt, Qt::NoModifier);
  CHECK(field->cursor().shape() == field_shape);
  send_key(window, QEvent::KeyPress, Qt::Key_Alt, Qt::AltModifier);
  CHECK(shows_background_cursor(*field));
  send_mouse(*field, QEvent::MouseMove, middle + QPoint(3, 3), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
  CHECK(field->cursor().shape() == field_shape);

  // An Alt press keeps the background cursor for its whole drag, Alt released or not.
  send_mouse(*field, QEvent::MouseButtonPress, middle, Qt::LeftButton, Qt::LeftButton, Qt::AltModifier);
  CHECK(shows_background_cursor(*field));
  send_key(window, QEvent::KeyRelease, Qt::Key_Alt, Qt::NoModifier);
  send_mouse(*field, QEvent::MouseMove, middle + QPoint(5, 0), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
  CHECK(shows_background_cursor(*field));
  send_mouse(*field, QEvent::MouseButtonRelease, middle + QPoint(5, 0), Qt::LeftButton, Qt::NoButton,
             Qt::NoModifier);
  QApplication::processEvents();
  CHECK(field->cursor().shape() == field_shape);
  // A plain press keeps the normal cursor even if Alt goes down mid-drag.
  send_mouse(*field, QEvent::MouseButtonPress, middle, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
  send_key(window, QEvent::KeyPress, Qt::Key_Alt, Qt::AltModifier);
  CHECK(field->cursor().shape() == field_shape);
  send_mouse(*field, QEvent::MouseButtonRelease, middle, Qt::LeftButton, Qt::NoButton, Qt::AltModifier);
  QApplication::processEvents();
  // Released with Alt still down: the next click would set the background.
  CHECK(shows_background_cursor(*field));
  QEvent leave(QEvent::Leave);
  QApplication::sendEvent(field, &leave);
  CHECK(field->cursor().shape() == field_shape);
  // Off the field, Alt changes nothing there.
  send_key(window, QEvent::KeyPress, Qt::Key_Alt, Qt::AltModifier);
  CHECK(field->cursor().shape() == field_shape);
  send_key(window, QEvent::KeyRelease, Qt::Key_Alt, Qt::NoModifier);

  // The hue strip does the same.
  hover(*strip, QPoint(strip->width() / 2, strip->height() / 2), Qt::AltModifier);
  CHECK(shows_background_cursor(*strip));
  send_key(window, QEvent::KeyRelease, Qt::Key_Alt, Qt::NoModifier);
  CHECK(strip->cursor().shape() == strip_shape);

  // The Background Color window, which already edits the background, keeps its cursor.
  panel->background_chip()->click();
  QApplication::processEvents();
  auto* dialog = window.findChild<QDialog*>(QStringLiteral("patchyColorDialog"));
  CHECK(dialog != nullptr);
  if (dialog != nullptr) {
    auto* plane = dialog->findChild<QWidget*>(QStringLiteral("patchyColorPlane"));
    CHECK(plane != nullptr);
    if (plane != nullptr) {
      hover(*plane, QPoint(20, 20), Qt::AltModifier);
      CHECK(plane->cursor().shape() == Qt::CrossCursor);
    }
    dialog->close();
    QApplication::processEvents();
  }
}

void ui_palette_panel_alt_click_sets_background() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = MainWindowTestAccess::document(window);
  const auto* preset = patchy::find_builtin_palette_preset("pico8");
  CHECK(preset != nullptr);
  if (preset == nullptr) {
    return;
  }
  patchy::DocumentPaletteEditing editing;
  editing.palette.colors.assign(preset->colors.begin(), preset->colors.end());
  editing.palette_revision = 903;
  document.palette_editing() = editing;
  MainWindowTestAccess::refresh_document_info(window);
  QApplication::processEvents();
  auto* grid = window.findChild<QWidget*>(QStringLiteral("paletteSwatchGrid"));
  CHECK(grid != nullptr);
  if (grid == nullptr) {
    return;
  }
  // Cell n center: 12 cells per row, 18 px cells with a 2 px gap.
  const auto cell = [](int index) { return QPoint(index * 20 + 9, 9); };
  const auto color_of = [&](int index) {
    const auto& entry = preset->colors[static_cast<std::size_t>(index)];
    return QColor(entry.red, entry.green, entry.blue);
  };

  const auto normal_shape = grid->cursor().shape();
  CHECK(normal_shape != Qt::BitmapCursor);

  // A plain click sets the foreground, as before.
  const auto background_before = canvas->secondary_color();
  click(*grid, cell(2));
  CHECK(canvas->primary_color().rgb() == color_of(2).rgb());
  CHECK(canvas->secondary_color().rgb() == background_before.rgb());

  // Alt-click sets the background and leaves the foreground.
  click(*grid, cell(5), Qt::AltModifier);
  CHECK(canvas->secondary_color().rgb() == color_of(5).rgb());
  CHECK(canvas->primary_color().rgb() == color_of(2).rgb());
  CHECK(window.statusBar()->currentMessage() == QStringLiteral("Background color changed"));
  // The Color panel's background chip follows.
  if (auto* panel = window.findChild<ColorDockPanel*>(QStringLiteral("colorPanel")); panel != nullptr) {
    CHECK(panel->background_chip()->color().rgb() == color_of(5).rgb());
  }

  // Released with Alt still down, the grid shows the background cursor; Alt over the
  // swatches shows it too, and releasing the key brings the normal one back.
  CHECK(shows_background_cursor(*grid));
  send_key(window, QEvent::KeyRelease, Qt::Key_Alt, Qt::NoModifier);
  CHECK(grid->cursor().shape() == normal_shape);
  hover(*grid, cell(3), Qt::AltModifier);
  CHECK(shows_background_cursor(*grid));
  send_key(window, QEvent::KeyRelease, Qt::Key_Alt, Qt::NoModifier);
  CHECK(grid->cursor().shape() == normal_shape);
  CHECK(!shows_background_cursor(*grid));
}

QDialog* open_color_window(patchy::ui::MainWindow& window, const char* chip_name) {
  auto* chip = window.findChild<QAbstractButton*>(QLatin1String(chip_name));
  CHECK(chip != nullptr);
  if (chip == nullptr) {
    return nullptr;
  }
  chip->click();
  QApplication::processEvents();
  auto* dialog = window.findChild<QDialog*>(QStringLiteral("patchyColorDialog"));
  CHECK(dialog != nullptr && dialog->isVisible());
  return dialog;
}

// The picker window's palette swatch `index` (24 px cells with a 5 px gap while the palette
// has at most 64 colors).
QPoint picker_cell(int index) { return QPoint(index * 29 + 12, 12); }

void ui_color_picker_foreground_window_alt_pick_sets_background() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* dialog = open_color_window(window, "foregroundColorButton");
  if (dialog == nullptr) {
    return;
  }
  CHECK(dialog->property("patchy.colorTarget").toString() == QStringLiteral("foreground"));
  auto* picker = dialog->findChild<patchy::ui::PatchyColorPicker*>(QStringLiteral("patchyAdvancedColorPicker"));
  auto* grid = dialog->findChild<QWidget*>(QStringLiteral("patchyColorPaletteGrid"));
  auto* plane = dialog->findChild<QWidget*>(QStringLiteral("patchyColorPlane"));
  CHECK(picker != nullptr && grid != nullptr && plane != nullptr);
  if (picker == nullptr || grid == nullptr || plane == nullptr) {
    return;
  }

  // Plain clicks on two swatches set the foreground, as before.
  click(*grid, picker_cell(0));
  const auto first = canvas->primary_color();
  click(*grid, picker_cell(1));
  const auto second = canvas->primary_color();
  CHECK(first.rgb() != second.rgb());
  const auto background_before = canvas->secondary_color();
  CHECK(background_before.rgb() != first.rgb());

  // Alt-click the first swatch: the background takes it; the foreground and the window's
  // own color stay on the second.
  click(*grid, picker_cell(0), Qt::AltModifier);
  CHECK(canvas->secondary_color().rgb() == first.rgb());
  CHECK(canvas->primary_color().rgb() == second.rgb());
  CHECK(picker->currentColor().rgb() == second.rgb());
  CHECK(window.statusBar()->currentMessage() == QStringLiteral("Background color changed"));

  // The Square field: an Alt-click gives the background the color a plain click there
  // gives the foreground.
  const QPoint spot(plane->width() * 3 / 4, plane->height() / 4);
  click(*plane, spot, Qt::AltModifier);
  const auto alt_picked = canvas->secondary_color();
  CHECK(canvas->primary_color().rgb() == second.rgb());
  CHECK(picker->currentColor().rgb() == second.rgb());
  click(*plane, spot);
  CHECK(canvas->primary_color().rgb() == alt_picked.rgb());
  CHECK(canvas->secondary_color().rgb() == alt_picked.rgb());

  // A custom color: Alt-click sets the background and leaves the slot unselected.
  auto* custom = dialog->findChild<QPushButton*>(QStringLiteral("patchyCustomColorSwatch"));
  CHECK(custom != nullptr);
  if (custom != nullptr) {
    const auto foreground = canvas->primary_color();
    click(*custom, QPoint(custom->width() / 2, custom->height() / 2), Qt::AltModifier);
    CHECK(canvas->primary_color().rgb() == foreground.rgb());
    CHECK(picker->currentColor().rgb() == foreground.rgb());
  }

  // Alt over the swatches shows the background cursor here.
  hover(*grid, picker_cell(2), Qt::AltModifier);
  CHECK(shows_background_cursor(*grid));
  send_key(window, QEvent::KeyRelease, Qt::Key_Alt, Qt::NoModifier);
  CHECK(grid->cursor().shape() == Qt::PointingHandCursor);
  dialog->close();
  QApplication::processEvents();
}

void ui_color_picker_background_window_alt_click_is_a_plain_click() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* dialog = open_color_window(window, "backgroundColorButton");
  if (dialog == nullptr) {
    return;
  }
  CHECK(dialog->property("patchy.colorTarget").toString() == QStringLiteral("background"));
  auto* grid = dialog->findChild<QWidget*>(QStringLiteral("patchyColorPaletteGrid"));
  CHECK(grid != nullptr);
  if (grid == nullptr) {
    return;
  }
  const auto foreground = canvas->primary_color();
  click(*grid, picker_cell(1));
  const auto plain = canvas->secondary_color();
  // Alt-click edits the background exactly like a plain click; the foreground stays.
  click(*grid, picker_cell(2), Qt::AltModifier);
  const auto alt = canvas->secondary_color();
  click(*grid, picker_cell(2));
  CHECK(canvas->secondary_color().rgb() == alt.rgb());
  CHECK(alt.rgb() != plain.rgb());
  CHECK(canvas->primary_color().rgb() == foreground.rgb());
  // No background cursor in this window.
  hover(*grid, picker_cell(3), Qt::AltModifier);
  CHECK(!shows_background_cursor(*grid));
  CHECK(grid->cursor().shape() == Qt::PointingHandCursor);
  send_key(window, QEvent::KeyRelease, Qt::Key_Alt, Qt::NoModifier);
  dialog->close();
  QApplication::processEvents();
}

using View = ColorDockPanel::View;
using Channel = ColorDockPanel::Channel;
const QString kColorPanelViewKey = QStringLiteral("colorPanel/view");

// The value a click at `x` on a channel track sets.
int track_value_at(QWidget& track, Channel channel, int x) {
  return patchy::ui::rounded_scaled_channel(std::clamp(x, 0, track.width() - 1), std::max(1, track.width() - 1),
                                            ColorDockPanel::channel_maximum(channel));
}

int checked_view_count(const ColorDockPanel& panel) {
  int checked = 0;
  for (const auto view : {View::HueCube, View::RgbSliders, View::HsbSliders, View::CmykSliders}) {
    checked += panel.view_action(view)->isChecked() ? 1 : 0;
  }
  return checked;
}

int percent_of(int byte_value) { return static_cast<int>(std::lround(byte_value * 100.0 / 255.0)); }

void ui_color_panel_view_menu_switches_views() {
  SettingsValueRestorer view_setting(kColorPanelViewKey);
  patchy::ui::app_settings().remove(kColorPanelViewKey);
  patchy::ui::MainWindow window;
  show_window(window);
  auto* panel = open_color_panel(window);
  auto* column = MainWindowTestAccess::panel_column(window);
  CHECK(column != nullptr);
  if (panel == nullptr || column == nullptr) {
    return;
  }
  // The Hue Cube is the default; the gear at the right end of the panel's title row lists
  // the views.
  CHECK(panel->view() == View::HueCube);
  CHECK(panel->view_action(View::HueCube)->isChecked());
  CHECK(checked_view_count(*panel) == 1);
  auto* button = panel->settings_button();
  CHECK(button->isVisible() && button->isEnabled());
  CHECK(button->popupMode() == QToolButton::InstantPopup);
  CHECK(button->menu() != nullptr);
  if (button->menu() != nullptr) {
    const auto entries = button->menu()->actions();
    CHECK(entries.size() == 4);
    if (entries.size() == 4) {
      CHECK(entries[0]->text() == QStringLiteral("Hue Cube"));
      CHECK(entries[1]->text() == QStringLiteral("RGB Sliders"));
      CHECK(entries[2]->text() == QStringLiteral("HSB Sliders"));
      CHECK(entries[3]->text() == QStringLiteral("CMYK Sliders"));
    }
  }
  CHECK(button->toolTip() == QStringLiteral("Color panel settings"));
  // In the Color panel's own title row, below the tab bar, at its right end; the row keeps
  // the height of the other title rows.
  auto* title = window.findChild<QWidget*>(QStringLiteral("colorDockTitle"));
  CHECK(title != nullptr && button->parentWidget() == title);
  if (title != nullptr) {
    CHECK(button->geometry().right() >= title->width() - 12);
    CHECK(title->rect().contains(button->geometry()));
    auto* palette_title = window.findChild<QWidget*>(QStringLiteral("paletteDockTitle"));
    CHECK(palette_title != nullptr && title->sizeHint().height() == palette_title->sizeHint().height());
  }
  auto* tabs = column->tab_bar(kColorPanel);
  CHECK(tabs != nullptr && !tabs->isAncestorOf(button));
  // Nothing is left under the pair, and the right-click menu no longer lists the views.
  CHECK(!panel->isAncestorOf(button));
  CHECK(panel->findChild<QWidget*>(QStringLiteral("colorPanelViewButton")) == nullptr);
  if (title != nullptr) {
    bool saw_menu = false;
    QTimer::singleShot(0, [&] {
      auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
      CHECK(menu != nullptr);
      if (menu == nullptr) {
        return;
      }
      saw_menu = true;
      CHECK(menu->objectName() == QStringLiteral("panelHeaderMenu"));
      for (const auto view : {View::HueCube, View::RgbSliders, View::HsbSliders, View::CmykSliders}) {
        CHECK(!menu->actions().contains(panel->view_action(view)));
      }
      menu->close();
    });
    const QPoint position(title->width() / 3, title->height() / 2);
    QContextMenuEvent event(QContextMenuEvent::Mouse, position, title->mapToGlobal(position));
    QApplication::sendEvent(title, &event);
    QApplication::processEvents();
    CHECK(saw_menu);
  }
  auto* section = column->section_widget(kColorPanel);
  const int cube_height = section->height();
  // A strong color, so every track shows a range.
  click(*panel->hue_strip(), QPoint(panel->hue_strip()->width() / 2, panel->hue_strip()->height() / 2));
  click(*panel->field(), QPoint(panel->field()->width() * 4 / 5, panel->field()->height() / 5));

  // RGB Sliders, picked from the gear's menu: three rows of track and number, the cube
  // hidden, and the section shorter.
  if (button->menu() != nullptr && button->menu()->actions().size() == 4) {
    button->menu()->actions()[1]->trigger();
  }
  QApplication::processEvents();
  CHECK(panel->view() == View::RgbSliders);
  CHECK(checked_view_count(*panel) == 1);
  CHECK(!panel->field()->isVisible());
  for (const auto channel : {Channel::Red, Channel::Green, Channel::Blue}) {
    CHECK(panel->channel_track(channel)->isVisible());
    CHECK(panel->channel_spin(channel)->isVisible());
    CHECK(panel->channel_spin(channel)->maximum() == 255);
  }
  CHECK(!panel->channel_track(Channel::Hue)->isVisible());
  const int rgb_height = section->height();
  CHECK(rgb_height < cube_height);
  // The rows fit: the last number box ends inside the panel.
  auto* blue = panel->channel_spin(Channel::Blue);
  CHECK(blue->mapTo(panel, QPoint(0, blue->height())).y() <= panel->height());
  save_widget_artifact("ui_color_panel_rgb_sliders", *section);

  // HSB Sliders: degrees and percent.
  panel->view_action(View::HsbSliders)->trigger();
  QApplication::processEvents();
  CHECK(panel->view() == View::HsbSliders);
  CHECK(checked_view_count(*panel) == 1);
  CHECK(panel->channel_track(Channel::Hue)->isVisible());
  CHECK(!panel->channel_track(Channel::Red)->isVisible());
  CHECK(panel->channel_spin(Channel::Hue)->maximum() == 359);
  CHECK(panel->channel_spin(Channel::Saturation)->maximum() == 100);
  CHECK(panel->channel_spin(Channel::Brightness)->maximum() == 100);
  CHECK(section->height() < cube_height);
  save_widget_artifact("ui_color_panel_hsb_sliders", *section);

  // CMYK Sliders: four rows of ink percent, all of them inside the panel, which grows by
  // the fourth row (taller than the cube's 170 with offscreen's tall number boxes).
  const int hsb_height = section->height();
  if (button->menu() != nullptr && button->menu()->actions().size() == 4) {
    button->menu()->actions()[3]->trigger();
  }
  QApplication::processEvents();
  CHECK(panel->view() == View::CmykSliders);
  CHECK(checked_view_count(*panel) == 1);
  for (const auto channel : {Channel::Cyan, Channel::Magenta, Channel::Yellow, Channel::Black}) {
    CHECK(panel->channel_track(channel)->isVisible());
    CHECK(panel->channel_spin(channel)->isVisible());
    CHECK(panel->channel_spin(channel)->maximum() == 100);
    CHECK(panel->channel_spin(channel)->suffix() == panel->channel_spin(Channel::Saturation)->suffix());
  }
  CHECK(!panel->channel_track(Channel::Hue)->isVisible());
  CHECK(section->height() > hsb_height);
  auto* black = panel->channel_spin(Channel::Black);
  CHECK(black->mapTo(panel, QPoint(0, black->height())).y() <= panel->height());
  save_widget_artifact("ui_color_panel_cmyk_sliders", *section);
  if (auto* menu = button->menu(); menu != nullptr) {
    menu->adjustSize();
    save_widget_artifact("ui_color_panel_view_menu", *menu);
  }

  // Back to the cube: it takes its preferred height back.
  panel->view_action(View::HueCube)->trigger();
  QApplication::processEvents();
  CHECK(panel->view() == View::HueCube);
  CHECK(panel->field()->isVisible() && panel->hue_strip()->isVisible());
  CHECK(section->height() > rgb_height);
  save_widget_artifact("ui_color_panel_hue_cube", *section);
}

void ui_color_panel_sliders_set_foreground() {
  SettingsValueRestorer view_setting(kColorPanelViewKey);
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* panel = open_color_panel(window);
  if (panel == nullptr) {
    return;
  }
  require_action(window, "colorDefaultAction")->trigger();
  QApplication::processEvents();
  panel->set_view(View::RgbSliders);
  QApplication::processEvents();
  const auto background = canvas->secondary_color();

  // A click on the red track sets red only.
  auto* red = panel->channel_track(Channel::Red);
  const int red_x = red->width() * 3 / 4;
  const int red_value = track_value_at(*red, Channel::Red, red_x);
  click(*red, QPoint(red_x, red->height() / 2));
  CHECK(canvas->primary_color().rgb() == QColor(red_value, 0, 0).rgb());
  CHECK(panel->channel_spin(Channel::Red)->value() == red_value);
  CHECK(canvas->secondary_color().rgb() == background.rgb());
  CHECK(window.statusBar()->currentMessage() == QStringLiteral("Foreground color changed"));
  // A drag follows the pointer.
  const int drag_x = red->width() / 4;
  send_mouse(*red, QEvent::MouseButtonPress, QPoint(red->width() / 2, 4), Qt::LeftButton, Qt::LeftButton,
             Qt::NoModifier);
  send_mouse(*red, QEvent::MouseMove, QPoint(drag_x, 4), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
  send_mouse(*red, QEvent::MouseButtonRelease, QPoint(drag_x, 4), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
  QApplication::processEvents();
  const int dragged = track_value_at(*red, Channel::Red, drag_x);
  CHECK(canvas->primary_color().red() == dragged);
  // A number sets its channel.
  panel->channel_spin(Channel::Green)->setValue(200);
  QApplication::processEvents();
  CHECK(canvas->primary_color().rgb() == QColor(dragged, 200, 0).rgb());
  panel->channel_spin(Channel::Blue)->setValue(40);
  QApplication::processEvents();
  CHECK(canvas->primary_color().rgb() == QColor(dragged, 200, 40).rgb());
  CHECK(canvas->secondary_color().rgb() == background.rgb());
  // The cube shows the same foreground, to its 8-bit HSV's precision.
  const auto cube = QColor::fromHsv(panel->hue(), panel->saturation(), panel->value());
  CHECK(std::abs(cube.red() - dragged) <= 2 && std::abs(cube.green() - 200) <= 2 && std::abs(cube.blue() - 40) <= 2);

  // HSB: the hue track turns the hue, keeping saturation and brightness.
  panel->set_view(View::HsbSliders);
  QApplication::processEvents();
  panel->channel_spin(Channel::Saturation)->setValue(80);
  panel->channel_spin(Channel::Brightness)->setValue(60);
  QApplication::processEvents();
  auto* hue = panel->channel_track(Channel::Hue);
  const int hue_x = hue->width() / 3;
  const int hue_value = track_value_at(*hue, Channel::Hue, hue_x);
  click(*hue, QPoint(hue_x, hue->height() / 2));
  const auto saturation = panel->saturation();
  const auto value = panel->value();
  CHECK(canvas->primary_color().rgb() == QColor::fromHsv(hue_value, saturation, value).rgb());
  CHECK(panel->channel_spin(Channel::Hue)->value() == hue_value);
  CHECK(panel->channel_spin(Channel::Saturation)->value() == 80);
  CHECK(panel->channel_spin(Channel::Brightness)->value() == 60);
  // Saturation 0 gives a gray and keeps the hue; back up, the same hue returns.
  panel->channel_spin(Channel::Saturation)->setValue(0);
  QApplication::processEvents();
  const auto gray = canvas->primary_color();
  CHECK(gray.red() == gray.green() && gray.green() == gray.blue());
  CHECK(panel->channel_spin(Channel::Hue)->value() == hue_value);
  panel->channel_spin(Channel::Saturation)->setValue(80);
  QApplication::processEvents();
  CHECK(canvas->primary_color().rgb() == QColor::fromHsv(hue_value, saturation, value).rgb());
  // Brightness 0 gives black and keeps hue and saturation.
  panel->channel_spin(Channel::Brightness)->setValue(0);
  QApplication::processEvents();
  CHECK(canvas->primary_color().rgb() == QColor(Qt::black).rgb());
  CHECK(panel->channel_spin(Channel::Hue)->value() == hue_value);
  CHECK(panel->channel_spin(Channel::Saturation)->value() == 80);
  panel->channel_spin(Channel::Brightness)->setValue(60);
  QApplication::processEvents();
  CHECK(canvas->primary_color().rgb() == QColor::fromHsv(hue_value, saturation, value).rgb());
  // An RGB edit that lands on a gray keeps the hue too.
  panel->set_view(View::RgbSliders);
  QApplication::processEvents();
  panel->channel_spin(Channel::Red)->setValue(90);
  panel->channel_spin(Channel::Green)->setValue(90);
  QApplication::processEvents();
  const auto hue_before_gray = panel->hue();
  panel->channel_spin(Channel::Blue)->setValue(90);
  QApplication::processEvents();
  CHECK(canvas->primary_color().rgb() == QColor(90, 90, 90).rgb());
  CHECK(panel->hue() == hue_before_gray);
  CHECK(canvas->secondary_color().rgb() == background.rgb());
}

void ui_color_panel_sliders_follow_color_changes_elsewhere() {
  SettingsValueRestorer view_setting(kColorPanelViewKey);
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* panel = open_color_panel(window);
  if (panel == nullptr) {
    return;
  }
  panel->set_view(View::RgbSliders);
  QApplication::processEvents();
  const auto shows = [&](QColor color) {
    return panel->channel_spin(Channel::Red)->value() == color.red() &&
           panel->channel_spin(Channel::Green)->value() == color.green() &&
           panel->channel_spin(Channel::Blue)->value() == color.blue();
  };
  int picks = 0;
  QObject::connect(panel, &ColorDockPanel::color_picked, panel, [&picks] { ++picks; });
  require_action(window, "colorDefaultAction")->trigger();
  QApplication::processEvents();
  CHECK(shows(QColor(Qt::black)));
  require_action(window, "colorSwapAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->primary_color().rgb() == QColor(Qt::white).rgb());
  CHECK(shows(QColor(Qt::white)));
  // A pick in the Foreground Color window moves the sliders.
  panel->foreground_chip()->click();
  QApplication::processEvents();
  auto* dialog = window.findChild<QDialog*>(QStringLiteral("patchyColorDialog"));
  CHECK(dialog != nullptr && dialog->isVisible());
  if (dialog != nullptr) {
    auto* plane = dialog->findChild<QWidget*>(QStringLiteral("patchyColorPlane"));
    auto* hue_slider = dialog->findChild<QWidget*>(QStringLiteral("patchyHueSlider"));
    CHECK(plane != nullptr && hue_slider != nullptr);
    if (plane != nullptr && hue_slider != nullptr) {
      click(*hue_slider, QPoint(4, hue_slider->height() / 3));
      click(*plane, QPoint(plane->width() * 2 / 3, plane->height() / 3));
      CHECK(canvas->primary_color().rgb() != QColor(Qt::white).rgb());
      CHECK(shows(canvas->primary_color()));
      panel->set_view(View::HsbSliders);
      QApplication::processEvents();
      const auto picked = canvas->primary_color();
      CHECK(panel->channel_spin(Channel::Hue)->value() == picked.hsvHue());
      CHECK(panel->channel_spin(Channel::Saturation)->value() == percent_of(picked.hsvSaturation()));
      CHECK(panel->channel_spin(Channel::Brightness)->value() == percent_of(picked.value()));
    }
    dialog->close();
    QApplication::processEvents();
  }
  // Showing a color is never a pick of its own: no feedback loop.
  CHECK(picks == 0);
  // A pick in the cube shows in the sliders.
  panel->set_view(View::HueCube);
  QApplication::processEvents();
  click(*panel->field(), QPoint(panel->field()->width() / 2, panel->field()->height() / 2));
  CHECK(picks == 1);
  panel->set_view(View::RgbSliders);
  QApplication::processEvents();
  CHECK(shows(canvas->primary_color()));
}

void ui_color_panel_view_persists() {
  SettingsValueRestorer view_setting(kColorPanelViewKey);
  patchy::ui::app_settings().remove(kColorPanelViewKey);
  {
    patchy::ui::MainWindow window;
    show_window(window);
    auto* panel = open_color_panel(window);
    if (panel == nullptr) {
      return;
    }
    panel->view_action(View::HsbSliders)->trigger();
    QApplication::processEvents();
    CHECK(patchy::ui::app_settings().value(kColorPanelViewKey).toString() == QStringLiteral("hsbSliders"));
  }
  {
    patchy::ui::MainWindow window;
    show_window(window);
    MainWindowTestAccess::show_panel(window, kColorPanel);
    QApplication::processEvents();
    auto* panel = window.findChild<ColorDockPanel*>(QStringLiteral("colorPanel"));
    CHECK(panel != nullptr);
    if (panel == nullptr) {
      return;
    }
    CHECK(panel->view() == View::HsbSliders);
    CHECK(panel->view_action(View::HsbSliders)->isChecked());
    CHECK(checked_view_count(*panel) == 1);
    CHECK(!panel->field()->isVisible());
    CHECK(panel->channel_track(Channel::Hue)->isVisible());
    // Opened in a slider view, the panel is shorter than the cube's preferred height.
    auto* column = MainWindowTestAccess::panel_column(window);
    CHECK(column != nullptr && column->section_widget(kColorPanel)->height() < 170);
    panel->view_action(View::RgbSliders)->trigger();
    QApplication::processEvents();
    CHECK(patchy::ui::app_settings().value(kColorPanelViewKey).toString() == QStringLiteral("rgbSliders"));
  }
  // CMYK Sliders is remembered too.
  CHECK(ColorDockPanel::view_from_token(QStringLiteral("cmykSliders")) == View::CmykSliders);
  CHECK(ColorDockPanel::view_token(View::CmykSliders) == QStringLiteral("cmykSliders"));
  // An unknown value opens the Hue Cube.
  patchy::ui::app_settings().setValue(kColorPanelViewKey, QStringLiteral("brightnessCube"));
  {
    patchy::ui::MainWindow window;
    auto* panel = window.findChild<ColorDockPanel*>(QStringLiteral("colorPanel"));
    CHECK(panel != nullptr && panel->view() == View::HueCube);
  }
}

// The inks the Color Picker window shows for `color`, in the working CMYK space.
std::array<int, 4> picker_inks(QColor color) {
  const auto ink = patchy::working_cmyk_space()->cmyk_from_rgb(
      {static_cast<std::uint8_t>(color.red()), static_cast<std::uint8_t>(color.green()),
       static_cast<std::uint8_t>(color.blue())});
  return {static_cast<int>(std::lround(ink.cyan)), static_cast<int>(std::lround(ink.magenta)),
          static_cast<int>(std::lround(ink.yellow)), static_cast<int>(std::lround(ink.black))};
}

QColor color_of_inks(const std::array<int, 4>& inks) {
  const auto rgb = patchy::working_cmyk_space()->rgb_from_cmyk(
      {static_cast<double>(inks[0]), static_cast<double>(inks[1]), static_cast<double>(inks[2]),
       static_cast<double>(inks[3])});
  return QColor(rgb.red, rgb.green, rgb.blue);
}

std::array<int, 4> shown_inks(const ColorDockPanel& panel) {
  return {panel.channel_spin(Channel::Cyan)->value(), panel.channel_spin(Channel::Magenta)->value(),
          panel.channel_spin(Channel::Yellow)->value(), panel.channel_spin(Channel::Black)->value()};
}

// The #404 follow-up: the CMYK sliders show the Color Picker window's numbers, set the
// foreground through the working space, keep inks as set, and follow changes elsewhere.
void ui_color_panel_cmyk_sliders_set_foreground() {
  SettingsValueRestorer view_setting(kColorPanelViewKey);
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* panel = open_color_panel(window);
  if (panel == nullptr) {
    return;
  }
  require_action(window, "colorDefaultAction")->trigger();
  QApplication::processEvents();
  panel->set_view(View::CmykSliders);
  QApplication::processEvents();
  const auto background = canvas->secondary_color();
  int picks = 0;
  QObject::connect(panel, &ColorDockPanel::color_picked, panel, [&picks] { ++picks; });
  CHECK(shown_inks(*panel) == picker_inks(QColor(Qt::black)));

  // Numbers set the inks, and the foreground is the color they print as.
  const std::array<int, 4> typed{30, 30, 30, 30};
  panel->channel_spin(Channel::Cyan)->setValue(typed[0]);
  panel->channel_spin(Channel::Magenta)->setValue(typed[1]);
  panel->channel_spin(Channel::Yellow)->setValue(typed[2]);
  panel->channel_spin(Channel::Black)->setValue(typed[3]);
  QApplication::processEvents();
  CHECK(canvas->primary_color().rgb() == color_of_inks(typed).rgb());
  // The inks stay as typed, whatever RGB would convert back to.
  CHECK(shown_inks(*panel) == typed);
  CHECK(canvas->secondary_color().rgb() == background.rgb());
  CHECK(window.statusBar()->currentMessage() == QStringLiteral("Foreground color changed"));

  // A click on the magenta track sets magenta only.
  auto* magenta = panel->channel_track(Channel::Magenta);
  const int magenta_x = magenta->width() * 4 / 5;
  const int magenta_value = track_value_at(*magenta, Channel::Magenta, magenta_x);
  click(*magenta, QPoint(magenta_x, magenta->height() / 2));
  const std::array<int, 4> clicked{typed[0], magenta_value, typed[2], typed[3]};
  CHECK(shown_inks(*panel) == clicked);
  CHECK(canvas->primary_color().rgb() == color_of_inks(clicked).rgb());
  // The track shows the color each magenta level gives, the others held.
  CHECK(panel->channel_color(Channel::Magenta, 0).rgb() == color_of_inks({typed[0], 0, typed[2], typed[3]}).rgb());
  // A drag on the black track follows the pointer.
  auto* black = panel->channel_track(Channel::Black);
  const int drag_x = black->width() / 5;
  send_mouse(*black, QEvent::MouseButtonPress, QPoint(black->width() / 2, 4), Qt::LeftButton, Qt::LeftButton,
             Qt::NoModifier);
  send_mouse(*black, QEvent::MouseMove, QPoint(drag_x, 4), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
  send_mouse(*black, QEvent::MouseButtonRelease, QPoint(drag_x, 4), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
  QApplication::processEvents();
  const std::array<int, 4> dragged{typed[0], magenta_value, typed[2], track_value_at(*black, Channel::Black, drag_x)};
  CHECK(shown_inks(*panel) == dragged);
  CHECK(canvas->primary_color().rgb() == color_of_inks(dragged).rgb());
  const int own_picks = picks;
  CHECK(own_picks > 0);

  // A color from elsewhere shows as the picker window's numbers, and is no pick.
  require_action(window, "colorSwapAction")->trigger();
  QApplication::processEvents();
  CHECK(shown_inks(*panel) == picker_inks(canvas->primary_color()));
  require_action(window, "colorSwapAction")->trigger();
  QApplication::processEvents();
  CHECK(shown_inks(*panel) == picker_inks(canvas->primary_color()));
  panel->set_view(View::HueCube);
  QApplication::processEvents();
  click(*panel->field(), QPoint(panel->field()->width() * 2 / 3, panel->field()->height() / 3));
  panel->set_view(View::CmykSliders);
  QApplication::processEvents();
  CHECK(shown_inks(*panel) == picker_inks(canvas->primary_color()));
  CHECK(picks == own_picks + 1);
  // The Foreground Color window shows the same numbers.
  panel->foreground_chip()->click();
  QApplication::processEvents();
  auto* dialog = window.findChild<QDialog*>(QStringLiteral("patchyColorDialog"));
  CHECK(dialog != nullptr);
  if (dialog != nullptr) {
    const std::array<const char*, 4> names{"patchyColorCyanSpin", "patchyColorMagentaSpin", "patchyColorYellowSpin",
                                           "patchyColorBlackSpin"};
    for (std::size_t index = 0; index < names.size(); ++index) {
      auto* spin = dialog->findChild<QSpinBox*>(QLatin1String(names[index]));
      CHECK(spin != nullptr && spin->value() == shown_inks(*panel)[index]);
    }
    dialog->close();
    QApplication::processEvents();
  }
}

// GitHub issue 464: a drag in the field sets the foreground on every move, but no move
// renders the field's gradient again (its hue does not change), writes the settings file
// or restyles the gradient preview; the release writes the pair once.
void ui_color_panel_field_drag_keeps_per_move_work_small() {
  const QString foreground_key = QStringLiteral("tools/foregroundColor");
  SettingsValueRestorer foreground_setting(foreground_key);
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* panel = open_color_panel(window);
  if (panel == nullptr) {
    return;
  }
  auto* field = panel->field();
  auto* gradient_preview = window.findChild<QWidget*>(QStringLiteral("gradientPreviewButton"));
  CHECK(gradient_preview != nullptr);
  click(*panel->hue_strip(), QPoint(4, panel->hue_strip()->height() / 3));
  click(*field, QPoint(field->width() / 2, field->height() / 2));
  (void)field->grab();
  const auto renders = field->plane_render_count();
  const auto stored_before = patchy::ui::app_settings().value(foreground_key).toString();
  CHECK(stored_before == canvas->primary_color().name(QColor::HexRgb));
  const auto preview_style_before = gradient_preview != nullptr ? gradient_preview->styleSheet() : QString();

  const QPoint start(field->width() / 5, field->height() / 5);
  send_mouse(*field, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
  QApplication::processEvents();
  CHECK(panel->picking());
  constexpr int kMoves = 40;
  bool every_move_applied = true;
  QPoint last = start;
  for (int step = 1; step <= kMoves; ++step) {
    last = QPoint(field->width() / 5 + (field->width() * 3 / 5) * step / kMoves,
                  field->height() / 5 + (field->height() * 3 / 5) * step / kMoves);
    send_mouse(*field, QEvent::MouseMove, last, Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::processEvents();
    (void)field->grab();
    const auto expected = field_color_at(panel->hue(), last, field->size());
    every_move_applied = every_move_applied && canvas->primary_color().rgb() == expected.rgb() &&
                         panel->foreground_chip()->color().rgb() == expected.rgb();
  }
  CHECK(every_move_applied);
  // The crosshair is where the pointer is, to the field's 8-bit saturation and value.
  const double marker_x = panel->saturation() / 255.0 * std::max(1, field->width() - 1);
  const double marker_y = (255 - panel->value()) / 255.0 * std::max(1, field->height() - 1);
  CHECK(std::abs(marker_x - last.x()) <= 1.0 && std::abs(marker_y - last.y()) <= 1.0);
  CHECK(field->plane_render_count() == renders);
  CHECK(patchy::ui::app_settings().value(foreground_key).toString() == stored_before);
  if (gradient_preview != nullptr) {
    CHECK(gradient_preview->styleSheet() == preview_style_before);
  }

  send_mouse(*field, QEvent::MouseButtonRelease, last, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
  QApplication::processEvents();
  CHECK(!panel->picking());
  CHECK(patchy::ui::app_settings().value(foreground_key).toString() == canvas->primary_color().name(QColor::HexRgb));
  if (gradient_preview != nullptr) {
    // The default gradient runs from the foreground, so its preview follows at the release.
    CHECK(gradient_preview->styleSheet() != preview_style_before);
  }
  // A hue change renders the gradient again, once.
  click(*panel->hue_strip(), QPoint(4, panel->hue_strip()->height() * 2 / 3));
  (void)field->grab();
  CHECK(field->plane_render_count() == renders + 1);
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
      {"ui_color_panel_alt_shows_background_cursor", ui_color_panel_alt_shows_background_cursor},
      {"ui_palette_panel_alt_click_sets_background", ui_palette_panel_alt_click_sets_background},
      {"ui_color_picker_foreground_window_alt_pick_sets_background",
       ui_color_picker_foreground_window_alt_pick_sets_background},
      {"ui_color_picker_background_window_alt_click_is_a_plain_click",
       ui_color_picker_background_window_alt_click_is_a_plain_click},
      {"ui_color_panel_view_menu_switches_views", ui_color_panel_view_menu_switches_views},
      {"ui_color_panel_sliders_set_foreground", ui_color_panel_sliders_set_foreground},
      {"ui_color_panel_sliders_follow_color_changes_elsewhere",
       ui_color_panel_sliders_follow_color_changes_elsewhere},
      {"ui_color_panel_view_persists", ui_color_panel_view_persists},
      {"ui_color_panel_cmyk_sliders_set_foreground", ui_color_panel_cmyk_sliders_set_foreground},
      {"ui_color_panel_field_drag_keeps_per_move_work_small", ui_color_panel_field_drag_keeps_per_move_work_small},
  };
}
