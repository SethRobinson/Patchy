#include <QScrollBar>

#include "ui/canvas_widget.hpp"
#include "ui/brush_dynamics_popup.hpp"
#include "ui/brush_stroke_preview.hpp"
#include "ui/brush_tool_window.hpp"
#include "ui/theme_palette.hpp"
#include "ui/brush_tip_library.hpp"
#include "ui/brush_tip_manager_dialog.hpp"
#include "ui/brush_tip_picker.hpp"
#include "ui/default_brush_tips.hpp"
#include "ui/main_window.hpp"
#include "ui/app_settings.hpp"
#include "test_harness.hpp"
#include "local_psd_fixtures.hpp"
#include <QApplication>
#include <QTest>
#include <QScopeGuard>
#include <QLineEdit>
#include <QKeyEvent>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QFile>
#include <QImage>
#include <QLabel>
#include <QListWidget>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSizeGrip>
#include <QSlider>
#include <QIODevice>
#include <QPainter>
#include <QPixmap>
#include <QPointer>
#include <QPushButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStringList>
#include <QScrollArea>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QWidget>
#include <algorithm>
#include <atomic>
#include <array>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include "ui_test_groups.hpp"
#include "ui_test_support.hpp"
#include "brush_pattern_palette_test_support.hpp"

namespace {

using namespace patchy::test::ui;

bool tool_settings_saved_brush_tip(const QString& tip_id) {
  return patchy::ui::app_settings().value(QStringLiteral("tools/brushTip")).toString() == tip_id;
}

void ui_brush_tip_manager_edits_dynamics() {
  clear_brush_tip_test_state();
  patchy::ui::MainWindow window;
  show_window(window);
  auto& library = window.brush_tip_library();
  const auto tip_id = library.add_tip(QStringLiteral("Bar"), make_bar_tip_image(), 0.25);
  CHECK(!tip_id.isEmpty());

  bool saw_editor = false;
  QTimer::singleShot(0, [&] {
    QDialog* manager = nullptr;
    for (auto* widget : QApplication::topLevelWidgets()) {
      if (widget->objectName() == QStringLiteral("brushTipManagerDialog")) {
        manager = qobject_cast<QDialog*>(widget);
      }
    }
    if (manager == nullptr) {
      return;
    }
    QApplication::processEvents();
    auto* dynamics_button = manager->findChild<QPushButton*>(QStringLiteral("brushTipManagerDynamicsButton"));
    CHECK(dynamics_button != nullptr);
    CHECK(dynamics_button->isEnabled());  // the initial tip is selected

    QTimer::singleShot(0, [&] {
      QDialog* editor = nullptr;
      for (auto* widget : QApplication::topLevelWidgets()) {
        if (widget->objectName() == QStringLiteral("brushTipManagerDynamicsDialog")) {
          editor = qobject_cast<QDialog*>(widget);
        }
      }
      if (editor == nullptr) {
        return;
      }
      saw_editor = true;
      editor->findChild<QSpinBox*>(QStringLiteral("dynamicsScatterSpin"))->setValue(200);
      editor->findChild<QSpinBox*>(QStringLiteral("dynamicsCountSpin"))->setValue(4);
      process_events_for(300);  // let the debounced apply run
      save_widget_artifact("ui_brush_tip_manager_dynamics_editor", *editor);
      editor->reject();
    });
    dynamics_button->click();  // blocks in the editor's exec loop until the inner callback closes it
    manager->reject();
  });
  patchy::ui::request_brush_tip_manager(&window, library, tip_id, {}, {});

  CHECK(saw_editor);
  const auto* entry = library.find_entry(tip_id);
  CHECK(entry != nullptr);
  CHECK(std::abs(entry->dynamics.scatter - 2.00) < 1e-9);
  CHECK(entry->dynamics.count == 4);
  CHECK(patchy::ui::brush_tip_entry_has_dynamics(*entry));
  // The badge marks dynamic tips: the decorated thumbnail differs from the plain one.
  CHECK(patchy::ui::brush_tip_thumbnail_with_badge(*entry).toImage() != entry->thumbnail.toImage());
  clear_brush_tip_test_state();
}

void ui_brush_tip_picker_popup_resizes_and_persists() {
  clear_brush_tip_test_state();
  {
    auto settings = patchy::ui::app_settings();
    settings.remove(QStringLiteral("ui/brushTipPickerPopupSize"));
    settings.sync();
  }
  patchy::ui::MainWindow window;
  show_window(window);
  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  QApplication::processEvents();
  auto* picker = window.findChild<patchy::ui::BrushTipPicker*>(QStringLiteral("brushTipPicker"));
  CHECK(picker != nullptr);

  const auto find_popup = []() -> QWidget* {
    QWidget* popup = nullptr;
    for (auto* widget : QApplication::topLevelWidgets()) {
      if (widget->objectName() == QStringLiteral("brushTipPickerPopup") && widget->isVisible()) {
        popup = widget;
      }
    }
    return popup;
  };

  picker->click();
  QApplication::processEvents();
  auto* popup = find_popup();
  CHECK(popup != nullptr);
  CHECK(popup->findChild<QSizeGrip*>() != nullptr);  // the resize handle
  auto* list = popup->findChild<QListWidget*>(QStringLiteral("brushTipPickerList"));
  CHECK(list != nullptr);
  CHECK(list->maximumWidth() == QWIDGETSIZE_MAX);  // no longer a fixed grid
  CHECK(popup->width() >= 5 * 70 + 28);            // first-open default keeps the classic footprint

  popup->resize(700, 520);
  QApplication::processEvents();
  popup->close();
  QApplication::processEvents();
  CHECK(patchy::ui::app_settings().value(QStringLiteral("ui/brushTipPickerPopupSize")).toSize() ==
        QSize(700, 520));

  picker->click();
  QApplication::processEvents();
  auto* reopened = find_popup();
  CHECK(reopened != nullptr);
  CHECK(reopened->size() == QSize(700, 520));
  reopened->close();
  QApplication::processEvents();
  clear_brush_tip_test_state();
}

void ui_brush_tip_cursor_shows_tip_shape() {
  clear_brush_tip_test_state();
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  canvas->set_brush_size(48);

  const auto opaque_bounds = [](const QPixmap& pixmap) {
    const auto image = pixmap.toImage();
    QRect bounds;
    for (int y = 0; y < image.height(); ++y) {
      for (int x = 0; x < image.width(); ++x) {
        if (qAlpha(image.pixel(x, y)) > 0) {
          bounds = bounds.isNull() ? QRect(x, y, 1, 1) : bounds.united(QRect(x, y, 1, 1));
        }
      }
    }
    return bounds;
  };

  // Procedural round brush: the cursor outline is a circle, roughly square bounds.
  const auto round_bounds = opaque_bounds(canvas->cursor().pixmap());
  CHECK(!round_bounds.isNull());
  CHECK(std::abs(round_bounds.width() - round_bounds.height()) <= 6);

  // A wide flat tip must produce a clearly wide, short outline instead.
  auto& library = window.brush_tip_library();
  const auto tip_id = library.add_tip(QStringLiteral("Bar"), make_bar_tip_image(), 0.25);
  CHECK(!tip_id.isEmpty());
  window.set_active_brush_tip(tip_id, false);
  const auto bar_bounds = opaque_bounds(canvas->cursor().pixmap());
  CHECK(!bar_bounds.isNull());
  CHECK(bar_bounds.width() > bar_bounds.height() * 2);

  // Back to round: the circle cursor returns.
  window.set_active_brush_tip(patchy::ui::builtin_round_brush_tip_id(), false);
  const auto restored_bounds = opaque_bounds(canvas->cursor().pixmap());
  CHECK(std::abs(restored_bounds.width() - restored_bounds.height()) <= 6);
  clear_brush_tip_test_state();
}

void ui_brush_tip_picker_popup_offers_define_from_selection() {
  clear_brush_tip_test_state();
  patchy::ui::MainWindow window;
  show_window(window);
  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  QApplication::processEvents();
  auto* picker = window.findChild<patchy::ui::BrushTipPicker*>(QStringLiteral("brushTipPicker"));
  CHECK(picker != nullptr);

  picker->click();
  QApplication::processEvents();
  QWidget* popup = nullptr;
  for (auto* widget : QApplication::topLevelWidgets()) {
    if (widget->objectName() == QStringLiteral("brushTipPickerPopup") && widget->isVisible()) {
      popup = widget;
    }
  }
  CHECK(popup != nullptr);
  auto* define_button = popup->findChild<QPushButton*>(QStringLiteral("brushTipDefineButton"));
  CHECK(define_button != nullptr);
  CHECK(define_button->isVisible());
  CHECK(popup->findChild<QPushButton*>(QStringLiteral("brushTipImportButton")) != nullptr);
  CHECK(popup->findChild<QPushButton*>(QStringLiteral("brushTipManageButton")) != nullptr);
  save_widget_artifact("ui_brush_tip_picker_popup", *popup);
  popup->close();
  QApplication::processEvents();
  clear_brush_tip_test_state();
}

void ui_brush_tip_picker_keeps_options_bar_height() {
  clear_brush_tip_test_state();
  patchy::ui::MainWindow window;
  show_window(window);
  // Wide enough that every tool's options fit on a single row.
  window.resize(1500, 800);
  QApplication::processEvents();

  auto* options_bar = window.findChild<QToolBar*>(QStringLiteral("Options"));
  CHECK(options_bar != nullptr);

  // The Options bar must keep one constant height across every tool, or the
  // canvas shifts vertically on each tool switch. Brush/Eraser are the
  // regression case: their Tip picker is a QToolButton, whose default style
  // made it the tallest control in the row.
  int common_height = 0;
  for (const char* tool : {"Move", "Marquee", "Lasso", "Magic Wand", "Clone", "Pattern Stamp",
                           "Healing Brush", "Smudge",
                           "Mixer Brush",
                           "Dodge", "Burn", "Sponge", "Fill", "Gradient", "Line", "Rect",
                           "Zoom", "Brush", "Eraser"}) {
    require_action_by_text(window, QString::fromLatin1(tool))->trigger();
    QApplication::processEvents();
    process_events_for(30);
    if (common_height == 0) {
      common_height = options_bar->height();
    }
    CHECK(options_bar->height() == common_height);
  }
  CHECK(common_height > 0);

  // Eraser is the active tool now, so the Tip picker is visible; it must not
  // exceed the shared 26px control height its neighbors use.
  auto* picker = window.findChild<patchy::ui::BrushTipPicker*>(QStringLiteral("brushTipPicker"));
  CHECK(picker != nullptr);
  CHECK(picker->isVisible());
  auto* preset_combo = window.findChild<QComboBox*>(QStringLiteral("brushPresetCombo"));
  CHECK(preset_combo != nullptr);
  CHECK(picker->height() <= preset_combo->height());
  save_widget_artifact("ui_brush_options_bar_height", *options_bar);

  // The Brush-only Dynamics button must obey the same 26px cap (QToolButton +3px size-hint
  // gotcha), and must not appear on the Eraser bar.
  auto* dynamics_button = window.findChild<QToolButton*>(QStringLiteral("brushDynamicsButton"));
  CHECK(dynamics_button != nullptr);
  CHECK(!dynamics_button->isVisible());  // Eraser is active
  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  QApplication::processEvents();
  process_events_for(30);
  CHECK(options_bar->height() == common_height);
  CHECK(dynamics_button->isVisible());
  CHECK(dynamics_button->height() <= preset_combo->height());
  clear_brush_tip_test_state();
}

// GitHub issue 364: the Tip picker took its width from the tip's name, so a long name
// (Brick Road) wrapped the Brush row onto a second line, and whether the Size slider
// showed depended on the tip current when the bar was first measured: starting on the
// Brush tool with a long tip hid it (and still kept two rows), switching to the Brush
// tool and then picking the tip kept it and wrapped. The picker now has one width for
// every tip, so both orders give the same one-row bar.
void ui_brush_tip_picker_keeps_a_fixed_width() {
  clear_brush_tip_test_state();
  SettingsValueRestorer saved_active_tool(QStringLiteral("tools/activeTool"));
  {
    auto settings = patchy::ui::app_settings();
    settings.remove(QStringLiteral("tools/activeTool"));
    settings.setValue(QStringLiteral("brushes/defaultTipsVersion"), 0);  // seed Brick Road
    settings.sync();
  }
  const auto settle = [] {
    QApplication::processEvents();
    process_events_for(30);
  };
  struct BarState {
    int height = 0;
    int lines = 0;
    bool slider = false;
    int picker_width = 0;
  };
  const auto bar_state = [](patchy::ui::MainWindow& window) {
    BarState state;
    auto* options_bar = window.findChild<QToolBar*>(QStringLiteral("Options"));
    auto* options = window.findChild<QWidget*>(QStringLiteral("OptionsContent"));
    auto* slider = window.findChild<QSlider*>(QStringLiteral("brushSizeSlider"));
    auto* picker = window.findChild<patchy::ui::BrushTipPicker*>(QStringLiteral("brushTipPicker"));
    CHECK(options_bar != nullptr && options != nullptr && slider != nullptr && picker != nullptr);
    if (options_bar == nullptr || options == nullptr || slider == nullptr || picker == nullptr) {
      return state;
    }
    std::vector<int> tops;
    for (auto* child : options->findChildren<QWidget*>(Qt::FindDirectChildrenOnly)) {
      const int top = child->geometry().top();
      if (child->isVisible() &&
          std::none_of(tops.begin(), tops.end(), [top](int other) { return std::abs(other - top) < 8; })) {
        tops.push_back(top);
      }
    }
    state.height = options_bar->height();
    state.lines = static_cast<int>(tops.size());
    state.slider = slider->isVisible();
    state.picker_width = picker->width();
    std::printf("  bar %d px, %d line(s), size slider %s, tip picker %d px\n", state.height, state.lines,
                state.slider ? "shown" : "hidden", state.picker_width);
    return state;
  };

  QString brick_road_id;
  BarState switched;
  {
    patchy::ui::MainWindow window;
    window.resize(1366, 738);
    show_window(window);
    window.resize(1366, 738);
    settle();
    auto& library = window.brush_tip_library();
    for (const auto& entry : library.entries()) {
      if (entry.name == QStringLiteral("Brick Road")) {
        brick_road_id = entry.id;
      }
    }
    CHECK(!brick_road_id.isEmpty());
    auto* picker = window.findChild<patchy::ui::BrushTipPicker*>(QStringLiteral("brushTipPicker"));
    auto* options_bar = window.findChild<QToolBar*>(QStringLiteral("Options"));
    CHECK(picker != nullptr && options_bar != nullptr);
    if (brick_road_id.isEmpty() || picker == nullptr || options_bar == nullptr) {
      clear_brush_tip_test_state();
      return;
    }
    require_action(window, "toolMoveAction")->trigger();
    settle();
    require_action(window, "toolBrushAction")->trigger();
    settle();
    const auto round = bar_state(window);
    CHECK(round.lines == 1);
    save_widget_artifact("ui_brush_options_bar_round_tip", *options_bar);

    window.set_active_brush_tip(brick_road_id, true);
    settle();
    switched = bar_state(window);
    save_widget_artifact("ui_brush_options_bar_switched_then_brick_road", *options_bar);
    CHECK(switched.picker_width == round.picker_width);
    CHECK(switched.lines == 1);
    CHECK(switched.height == round.height);
    CHECK(switched.slider == round.slider);
    CHECK(picker->toolTip().contains(QStringLiteral("Brick Road")));

    // A name far wider than the control is elided, not grown into.
    const auto long_id = library.add_tip(QStringLiteral("An Unusually Long Imported Brush Tip Name"),
                                         make_bar_tip_image(), 0.5);
    window.set_active_brush_tip(long_id, true);
    settle();
    const auto long_name = bar_state(window);
    CHECK(long_name.picker_width == round.picker_width);
    CHECK(long_name.lines == 1);
    CHECK(picker->toolTip().contains(QStringLiteral("An Unusually Long Imported Brush Tip Name")));

    window.set_active_brush_tip(brick_road_id, true);
    for (int waited = 0; waited < 5000 && !tool_settings_saved_brush_tip(brick_road_id); waited += 100) {
      process_events_for(100);  // the debounced tool-settings save records the tip
    }
    CHECK(patchy::ui::app_settings().value(QStringLiteral("tools/brushTip")).toString() == brick_road_id);
  }
  {
    // The next launch starts on the Brush tool with Brick Road: the same bar.
    patchy::ui::MainWindow window;
    window.resize(1366, 738);
    show_window(window);
    window.resize(1366, 738);
    settle();
    auto* picker = window.findChild<patchy::ui::BrushTipPicker*>(QStringLiteral("brushTipPicker"));
    auto* options_bar = window.findChild<QToolBar*>(QStringLiteral("Options"));
    CHECK(picker != nullptr && options_bar != nullptr);
    if (picker != nullptr && options_bar != nullptr) {
      CHECK(picker->current_tip_id() == brick_road_id);
      const auto started = bar_state(window);
      save_widget_artifact("ui_brush_options_bar_started_on_brick_road", *options_bar);
      CHECK(started.picker_width == switched.picker_width);
      CHECK(started.lines == 1);
      CHECK(started.height == switched.height);
      // Whether 1366 px keeps the Size slider depends on the widest tool row; what matters
      // is that both orders agree.
      CHECK(started.slider == switched.slider);
    }
  }
  clear_brush_tip_test_state();
}

void ui_default_brush_tips_carry_curated_dynamics() {
  clear_brush_tip_test_state();
  {
    auto settings = patchy::ui::app_settings();
    settings.setValue(QStringLiteral("brushes/defaultTipsVersion"), 0);
    settings.sync();
  }

  QString spatter_id;
  QString star_id;
  {
    patchy::ui::MainWindow window;
    show_window(window);
    auto& library = window.brush_tip_library();
    const auto folder = patchy::ui::default_brush_tips_folder_name();
    const auto entry_named = [&library, &folder](const QString& name) -> const patchy::ui::BrushTipEntry* {
      for (const auto& entry : library.entries()) {
        if (entry.folder == folder && entry.name == name) {
          return &entry;
        }
      }
      return nullptr;
    };

    // Fresh seeding lands the curated dynamics with the tips.
    const auto* spatter = entry_named(QStringLiteral("Spatter"));
    CHECK(spatter != nullptr);
    CHECK(spatter->dynamics.active());
    CHECK(std::abs(spatter->dynamics.scatter - 1.50) < 1e-9);
    CHECK(spatter->dynamics.count == 2);
    const auto* bristle = entry_named(QStringLiteral("Bristle"));
    CHECK(bristle != nullptr);
    CHECK(bristle->dynamics.angle_control == patchy::BrushDynamicControl::Direction);
    const auto* grass = entry_named(QStringLiteral("Grass"));
    CHECK(grass != nullptr);
    CHECK(grass->dynamics.angle_control == patchy::BrushDynamicControl::Direction);
    CHECK(grass->dynamics.count == 2);
    // Stability tips deliberately ship without dynamics (Dotted Line and the logo stamp clean).
    for (const auto* name : {"Canvas", "Square", "Calligraphy", "Dotted Line", "RTsoft Logo"}) {
      const auto* entry = entry_named(QString::fromLatin1(name));
      CHECK(entry != nullptr);
      CHECK(!entry->dynamics.active());
    }
    // v3 stamp tips: path stamps follow the stroke direction, scatter stamps scatter.
    const auto* brick = entry_named(QStringLiteral("Brick Road"));
    CHECK(brick != nullptr);
    CHECK(brick->dynamics.angle_control == patchy::BrushDynamicControl::Direction);
    CHECK(std::abs(brick->spacing - 1.0) < 1e-9);
    const auto* leaf = entry_named(QStringLiteral("Leaf"));
    CHECK(leaf != nullptr);
    CHECK(std::abs(leaf->dynamics.scatter - 1.50) < 1e-9);
    CHECK(leaf->dynamics.scatter_both_axes);
    CHECK(leaf->dynamics.count == 2);
    CHECK(leaf->dynamics.flip_x_jitter);
    CHECK(std::abs(leaf->dynamics.angle_jitter - 1.0) < 1e-9);
    const auto* rain = entry_named(QStringLiteral("Rain"));
    CHECK(rain != nullptr);
    CHECK(rain->dynamics.angle_control == patchy::BrushDynamicControl::Off);
    CHECK(rain->dynamics.angle_jitter == 0.0);  // streaks must stay parallel
    CHECK(std::abs(rain->dynamics.scatter - 2.50) < 1e-9);
    const auto* textured_chalk = entry_named(QStringLiteral("Textured Chalk"));
    CHECK(textured_chalk != nullptr);
    CHECK(textured_chalk->dynamics.texture_enabled);
    CHECK(textured_chalk->dynamics.texture_style == patchy::BrushTextureStyle::FineGrain);
    const auto* dual_dots = entry_named(QStringLiteral("Dual Brush Dots"));
    CHECK(dual_dots != nullptr);
    CHECK(dual_dots->dynamics.dual_brush_enabled);
    const auto* color_scatter = entry_named(QStringLiteral("Color Scatter"));
    CHECK(color_scatter != nullptr);
    CHECK(color_scatter->dynamics.color_dynamics_enabled);
    CHECK(std::abs(color_scatter->dynamics.foreground_background_jitter - 0.70) < 1e-9);
    const auto* wet_wash = entry_named(QStringLiteral("Wet Edge Wash"));
    CHECK(wet_wash != nullptr);
    CHECK(wet_wash->dynamics.wet_edges);

    // Prepare the migration scenario: one default tip "reset" to no dynamics (pre-dynamics
    // install state) and one customized by the user.
    spatter_id = spatter->id;
    const auto* star = entry_named(QStringLiteral("Star"));
    CHECK(star != nullptr);
    star_id = star->id;
    CHECK(library.set_tip_dynamics(spatter_id, {}, 0.0, 100.0));
    patchy::BrushDynamics custom;
    custom.scatter = 9.5;
    custom.count = 7;
    CHECK(library.set_tip_dynamics(star_id, custom, 0.0, 100.0));
  }
  {
    auto settings = patchy::ui::app_settings();
    settings.setValue(QStringLiteral("brushes/defaultTipsVersion"), 1);
    settings.sync();
  }

  // A window seeing version 1 runs the one-shot migration: untouched-default tips regain the
  // curated dynamics, customized tips keep the user's values.
  {
    patchy::ui::MainWindow window;
    show_window(window);
    auto& library = window.brush_tip_library();
    const auto* migrated = library.find_entry(spatter_id);
    CHECK(migrated != nullptr);
    CHECK(std::abs(migrated->dynamics.scatter - 1.50) < 1e-9);
    CHECK(migrated->dynamics.count == 2);
    const auto* custom = library.find_entry(star_id);
    CHECK(custom != nullptr);
    CHECK(std::abs(custom->dynamics.scatter - 9.5) < 1e-9);
    CHECK(custom->dynamics.count == 7);
    CHECK(patchy::ui::app_settings().value(QStringLiteral("brushes/defaultTipsVersion")).toInt() == 4);

    // "Restore Default Brushes" also un-messes customized defaults: spacing, tip shape, and
    // dynamics all reset to factory (the migration above deliberately left them alone).
    CHECK(library.set_tip_spacing(star_id, 0.77));
    CHECK(library.reset_default_tips_to_factory() == 1);  // only Star differs from its spec
    const auto* reset_star = library.find_entry(star_id);
    CHECK(reset_star != nullptr);
    CHECK(std::abs(reset_star->spacing - 1.30) < 1e-9);
    CHECK(std::abs(reset_star->dynamics.scatter - 2.00) < 1e-9);
    CHECK(reset_star->dynamics.count == 2);
    CHECK(reset_star->base_angle_degrees == 0.0);
    CHECK(reset_star->base_roundness == 100.0);
    CHECK(library.reset_default_tips_to_factory() == 0);  // idempotent once factory
  }
  clear_brush_tip_test_state();
}

void ui_brush_dynamics_round_brush_session() {
  clear_brush_tip_test_state();
  {
    patchy::ui::MainWindow window;
    show_window(window);
    auto* canvas = require_canvas(window);
    require_action_by_text(window, QStringLiteral("Brush"))->trigger();
    QApplication::processEvents();

    auto* button = window.findChild<QToolButton*>(QStringLiteral("brushDynamicsButton"));
    CHECK(button != nullptr);
    CHECK(button->isVisible());
    CHECK(button->isEnabled());  // the Round brush carries session-only dynamics
    CHECK(!canvas->brush_dynamics().active());

    // Popup edits apply to the canvas without touching the library.
    button->click();
    QApplication::processEvents();
    QWidget* popup = nullptr;
    for (auto* widget : QApplication::topLevelWidgets()) {
      if (widget->objectName() == QStringLiteral("brushDynamicsPopup") && widget->isVisible()) {
        popup = widget;
      }
    }
    CHECK(popup != nullptr);
    popup->findChild<QSpinBox*>(QStringLiteral("dynamicsScatterSpin"))->setValue(200);
    popup->findChild<QSpinBox*>(QStringLiteral("dynamicsCountSpin"))->setValue(2);
    popup->close();
    // The popup commits edits on a ~200ms debounce timer; wait for it rather than a fixed
    // sleep, which a loaded machine can outrun.
    CHECK(process_events_until([canvas] { return canvas->brush_dynamics().count == 2; }, 5000));
    CHECK(std::abs(canvas->brush_dynamics().scatter - 2.0) < 1e-9);
    CHECK(canvas->brush_dynamics().count == 2);
    CHECK(!canvas->has_brush_tip());                      // still the procedural Round brush
    CHECK(window.brush_tip_library().entries().empty());  // nothing persisted to the library

    // Switching to a bitmap tip and back preserves the session values.
    auto& library = window.brush_tip_library();
    const auto tip_id = library.add_tip(QStringLiteral("Bar"), make_bar_tip_image(), 0.25);
    CHECK(!tip_id.isEmpty());
    window.set_active_brush_tip(tip_id, false);
    CHECK(button->isEnabled());
    CHECK(!canvas->brush_dynamics().active());  // the Bar tip ships without dynamics
    window.set_active_brush_tip(patchy::ui::builtin_round_brush_tip_id(), false);
    CHECK(button->isEnabled());
    CHECK(std::abs(canvas->brush_dynamics().scatter - 2.0) < 1e-9);

    // With dynamics active the Round brush stamps through the synthesized disc tip: scattered
    // dabs land clearly off the stroke line.
    canvas->set_primary_color(QColor(0, 0, 0));
    canvas->set_brush_size(16);
    canvas->set_brush_softness(0);
    canvas->set_brush_dynamics_test_seed(42);
    const auto from = canvas->widget_position_for_document_point(QPoint(300, 200));
    const auto to = canvas->widget_position_for_document_point(QPoint(500, 200));
    drag(*canvas, from, to);
    QApplication::processEvents();
    const auto image = canvas->grab().toImage().convertToFormat(QImage::Format_RGB32);
    int off_line_dark = 0;
    for (int y = 160; y <= 240; y += 2) {
      if (std::abs(y - 200) < 12) {
        continue;  // the plain 16px round stroke corridor
      }
      for (int x = 280; x <= 520; x += 2) {
        const auto widget_point = canvas->widget_position_for_document_point(QPoint(x, y));
        if (!image.rect().contains(widget_point)) {
          continue;
        }
        if (QColor(image.pixel(widget_point)).lightness() < 100) {
          ++off_line_dark;
        }
      }
    }
    CHECK(off_line_dark > 5);
    save_widget_artifact("ui_brush_dynamics_round_scatter_stroke", *canvas);
    canvas->set_brush_dynamics_test_seed(std::nullopt);
  }
  // Session-only by design: a fresh window starts with a plain Round brush again.
  {
    patchy::ui::MainWindow window;
    show_window(window);
    auto* canvas = require_canvas(window);
    CHECK(!canvas->brush_dynamics().active());
  }
  clear_brush_tip_test_state();
}

void ui_brush_dynamics_popup_edits_apply_and_persist() {
  clear_brush_tip_test_state();
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  QApplication::processEvents();

  auto& library = window.brush_tip_library();
  const auto tip_id = library.add_tip(QStringLiteral("Bar"), make_bar_tip_image(), 0.25);
  CHECK(!tip_id.isEmpty());
  patchy::BrushDynamics seeded_dynamics;
  seeded_dynamics.texture_seed = 0x13579BDFU;
  CHECK(library.set_tip_dynamics(tip_id, seeded_dynamics, 0.0, 100.0));
  window.set_active_brush_tip(tip_id, false);

  auto* button = window.findChild<QToolButton*>(QStringLiteral("brushDynamicsButton"));
  CHECK(button != nullptr);
  CHECK(button->isEnabled());
  button->click();
  QApplication::processEvents();

  QWidget* popup = nullptr;
  for (auto* widget : QApplication::topLevelWidgets()) {
    if (widget->objectName() == QStringLiteral("brushDynamicsPopup") && widget->isVisible()) {
      popup = widget;
    }
  }
  CHECK(popup != nullptr);
  popup->findChild<QSpinBox*>(QStringLiteral("dynamicsSizeJitterSpin"))->setValue(40);
  popup->findChild<QSpinBox*>(QStringLiteral("dynamicsScatterSpin"))->setValue(150);
  popup->findChild<QSpinBox*>(QStringLiteral("dynamicsCountSpin"))->setValue(3);
  popup->findChild<QComboBox*>(QStringLiteral("dynamicsAngleControlCombo"))->setCurrentIndex(6);  // Direction
  popup->findChild<QSpinBox*>(QStringLiteral("dynamicsBaseAngleSpin"))->setValue(30);
  popup->findChild<QSpinBox*>(QStringLiteral("dynamicsOpacityJitterSpin"))->setValue(25);
  popup->findChild<QSpinBox*>(QStringLiteral("dynamicsFlowJitterSpin"))->setValue(35);
  auto* texture_enabled =
      popup->findChild<QCheckBox*>(QStringLiteral("dynamicsTextureEnabledCheck"));
  auto* texture_style =
      popup->findChild<QComboBox*>(QStringLiteral("dynamicsTextureStyleCombo"));
  CHECK(texture_enabled != nullptr);
  CHECK(texture_style != nullptr);
  texture_enabled->setChecked(true);
  texture_style->setCurrentIndex(
      texture_style->findData(static_cast<int>(patchy::BrushTextureStyle::Canvas)));
  popup->findChild<QSpinBox*>(QStringLiteral("dynamicsTextureScaleSpin"))->setValue(175);
  popup->findChild<QSpinBox*>(QStringLiteral("dynamicsTextureDepthSpin"))->setValue(65);
  popup->findChild<QCheckBox*>(QStringLiteral("dynamicsTextureInvertCheck"))->setChecked(true);
  popup->findChild<QCheckBox*>(QStringLiteral("dynamicsDualBrushEnabledCheck"))->setChecked(true);
  popup->findChild<QSpinBox*>(QStringLiteral("dynamicsDualBrushSizeSpin"))->setValue(35);
  popup->findChild<QSpinBox*>(QStringLiteral("dynamicsDualBrushHardnessSpin"))->setValue(70);
  popup->findChild<QSpinBox*>(QStringLiteral("dynamicsDualBrushSpacingSpin"))->setValue(125);
  popup->findChild<QCheckBox*>(QStringLiteral("dynamicsColorEnabledCheck"))->setChecked(true);
  popup->findChild<QSpinBox*>(QStringLiteral("dynamicsColorForegroundBackgroundJitterSpin"))
      ->setValue(60);
  popup->findChild<QSpinBox*>(QStringLiteral("dynamicsColorHueJitterSpin"))->setValue(15);
  popup->findChild<QSpinBox*>(QStringLiteral("dynamicsColorSaturationJitterSpin"))->setValue(25);
  popup->findChild<QSpinBox*>(QStringLiteral("dynamicsColorBrightnessJitterSpin"))->setValue(10);
  popup->findChild<QSpinBox*>(QStringLiteral("dynamicsColorPuritySpin"))->setValue(-20);
  popup->findChild<QCheckBox*>(QStringLiteral("dynamicsColorPerTipCheck"))->setChecked(false);
  auto* wet_edges = popup->findChild<QCheckBox*>(QStringLiteral("dynamicsWetEdgesCheck"));
  CHECK(wet_edges != nullptr);
  CHECK(wet_edges->toolTip().contains(QStringLiteral("does not smear"), Qt::CaseInsensitive));
  CHECK(wet_edges->toolTip().contains(QStringLiteral("Smudge")));
  wet_edges->setChecked(true);
  QApplication::processEvents();
  auto* panel = popup->findChild<patchy::ui::BrushDynamicsPanel*>();
  CHECK(panel != nullptr);
  panel->select_section(patchy::ui::BrushDynamicsPanel::Section::ShapeDynamics);
  QApplication::processEvents();
  save_widget_artifact("ui_brush_dynamics_popup", *popup);
  panel->select_section(patchy::ui::BrushDynamicsPanel::Section::Transfer);
  QApplication::processEvents();
  save_widget_artifact("ui_brush_dynamics_popup_transfer", *popup);
  popup->close();
  // One debounced (~200ms) commit applies every edit; wait for it to land, not a fixed sleep.
  CHECK(process_events_until([canvas] { return canvas->brush_dynamics().wet_edges; }, 5000));

  const auto& dynamics = canvas->brush_dynamics();
  CHECK(std::abs(dynamics.size_jitter - 0.40) < 1e-9);
  CHECK(std::abs(dynamics.scatter - 1.50) < 1e-9);
  CHECK(dynamics.count == 3);
  CHECK(dynamics.angle_control == patchy::BrushDynamicControl::Direction);
  CHECK(std::abs(dynamics.opacity_jitter - 0.25) < 1e-9);
  CHECK(std::abs(dynamics.flow_jitter - 0.35) < 1e-9);
  CHECK(dynamics.texture_enabled);
  CHECK(dynamics.texture_style == patchy::BrushTextureStyle::Canvas);
  CHECK(std::abs(dynamics.texture_scale - 1.75) < 1e-9);
  CHECK(std::abs(dynamics.texture_depth - 0.65) < 1e-9);
  CHECK(dynamics.texture_invert);
  CHECK(dynamics.texture_seed == 0x13579BDFU);  // hidden imported identity survives UI edits
  CHECK(dynamics.dual_brush_enabled);
  CHECK(std::abs(dynamics.dual_brush_size - 0.35) < 1e-9);
  CHECK(std::abs(dynamics.dual_brush_hardness - 0.70) < 1e-9);
  CHECK(std::abs(dynamics.dual_brush_spacing - 1.25) < 1e-9);
  CHECK(dynamics.color_dynamics_enabled);
  CHECK(std::abs(dynamics.foreground_background_jitter - 0.60) < 1e-9);
  CHECK(std::abs(dynamics.hue_jitter - 0.15) < 1e-9);
  CHECK(std::abs(dynamics.saturation_jitter - 0.25) < 1e-9);
  CHECK(std::abs(dynamics.brightness_jitter - 0.10) < 1e-9);
  CHECK(std::abs(dynamics.purity + 0.20) < 1e-9);
  CHECK(!dynamics.color_per_tip);
  CHECK(dynamics.wet_edges);
  CHECK(std::abs(canvas->brush_base_angle_degrees() - 30.0) < 1e-9);

  // The edit persisted to the tip's sidecar on disk.
  QFile sidecar(brush_tip_test_storage_dir() + QStringLiteral("/") + tip_id + QStringLiteral(".json"));
  CHECK(sidecar.open(QIODevice::ReadOnly));
  const auto object = QJsonDocument::fromJson(sidecar.readAll()).object();
  CHECK(object.value(QStringLiteral("baseAngle")).toDouble() == 30.0);
  const auto dynamics_json = object.value(QStringLiteral("dynamics")).toObject();
  CHECK(!dynamics_json.isEmpty());
  CHECK(std::abs(dynamics_json.value(QStringLiteral("sizeJitter")).toDouble() - 0.40) < 1e-9);
  CHECK(dynamics_json.value(QStringLiteral("angleControl")).toString() == QStringLiteral("direction"));
  CHECK(dynamics_json.value(QStringLiteral("count")).toInt() == 3);
  CHECK(std::abs(dynamics_json.value(QStringLiteral("flowJitter")).toDouble() - 0.35) < 1e-9);
  CHECK(dynamics_json.value(QStringLiteral("textureEnabled")).toBool());
  CHECK(dynamics_json.value(QStringLiteral("textureStyle")).toString() == QStringLiteral("canvas"));
  CHECK(dynamics_json.value(QStringLiteral("textureSeed")).toDouble() ==
        static_cast<double>(0x13579BDFU));
  CHECK(dynamics_json.value(QStringLiteral("dualBrushEnabled")).toBool());
  CHECK(dynamics_json.value(QStringLiteral("colorDynamicsEnabled")).toBool());
  CHECK(!dynamics_json.value(QStringLiteral("colorPerTip")).toBool(true));
  CHECK(dynamics_json.value(QStringLiteral("wetEdges")).toBool());

  // A fresh library over the same storage reads the identical dynamics (sidecar round trip);
  // a legacy sidecar without the new keys reads as defaults.
  patchy::ui::BrushTipLibrary second(brush_tip_test_storage_dir());
  const auto* reloaded = second.find_entry(tip_id);
  CHECK(reloaded != nullptr);
  CHECK(std::abs(reloaded->dynamics.scatter - 1.50) < 1e-9);
  CHECK(reloaded->dynamics.count == 3);
  CHECK(reloaded->dynamics.angle_control == patchy::BrushDynamicControl::Direction);
  CHECK(std::abs(reloaded->dynamics.flow_jitter - 0.35) < 1e-9);
  CHECK(reloaded->dynamics.texture_enabled);
  CHECK(reloaded->dynamics.texture_style == patchy::BrushTextureStyle::Canvas);
  CHECK(reloaded->dynamics.texture_seed == 0x13579BDFU);
  CHECK(reloaded->dynamics.dual_brush_enabled);
  CHECK(reloaded->dynamics.color_dynamics_enabled);
  CHECK(!reloaded->dynamics.color_per_tip);
  CHECK(reloaded->dynamics.wet_edges);
  CHECK(std::abs(reloaded->base_angle_degrees - 30.0) < 1e-9);
  const auto legacy_id = second.add_tip(QStringLiteral("Legacy"), make_bar_tip_image(), 0.25);
  const auto* legacy = second.find_entry(legacy_id);
  CHECK(legacy != nullptr);
  CHECK(!legacy->dynamics.active());
  clear_brush_tip_test_state();
}

void ui_brush_dynamics_popup_control_edits_persist() {
  clear_brush_tip_test_state();
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  QApplication::processEvents();

  auto& library = window.brush_tip_library();
  const auto tip_id = library.add_tip(QStringLiteral("Bar"), make_bar_tip_image(), 0.25);
  CHECK(!tip_id.isEmpty());
  window.set_active_brush_tip(tip_id, false);

  auto* button = window.findChild<QToolButton*>(QStringLiteral("brushDynamicsButton"));
  CHECK(button != nullptr);
  button->click();
  QApplication::processEvents();
  QWidget* popup = nullptr;
  for (auto* widget : QApplication::topLevelWidgets()) {
    if (widget->objectName() == QStringLiteral("brushDynamicsPopup") && widget->isVisible()) {
      popup = widget;
    }
  }
  CHECK(popup != nullptr);
  // A fixed size of about 520 x 600 (GitHub issue 506), clamped to the screen.
  CHECK(popup->height() <= 600);
  CHECK(popup->width() >= 520);

  const auto select_control = [popup](const char* combo_name, patchy::BrushDynamicControl control) {
    auto* combo = popup->findChild<QComboBox*>(QLatin1String(combo_name));
    CHECK(combo != nullptr);
    const auto index = combo->findData(static_cast<int>(control));
    CHECK(index >= 0);
    combo->setCurrentIndex(index);
  };
  // The data-mapped combos default to the slot's default as the first item.
  select_control("dynamicsSizeControlCombo", patchy::BrushDynamicControl::Off);
  select_control("dynamicsOpacityControlCombo", patchy::BrushDynamicControl::PenPressure);
  auto* minimum_opacity = popup->findChild<QSpinBox*>(QStringLiteral("dynamicsMinimumOpacitySpin"));
  CHECK(minimum_opacity != nullptr);
  CHECK(minimum_opacity->isEnabled());  // live once the opacity control has a source
  minimum_opacity->setValue(30);
  select_control("dynamicsFlowControlCombo", patchy::BrushDynamicControl::Fade);
  auto* minimum_flow = popup->findChild<QSpinBox*>(QStringLiteral("dynamicsMinimumFlowSpin"));
  auto* flow_fade = popup->findChild<QSpinBox*>(QStringLiteral("dynamicsFlowFadeStepsSpin"));
  CHECK(minimum_flow != nullptr);
  CHECK(flow_fade != nullptr);
  CHECK(minimum_flow->isEnabled());
  CHECK(!flow_fade->isHidden());  // on the Transfer page, which need not be the current tab
  minimum_flow->setValue(15);
  flow_fade->setValue(45);
  auto* scatter_fade = popup->findChild<QSpinBox*>(QStringLiteral("dynamicsScatterFadeStepsSpin"));
  CHECK(scatter_fade != nullptr);
  CHECK(scatter_fade->isHidden());
  select_control("dynamicsScatterControlCombo", patchy::BrushDynamicControl::Fade);
  QApplication::processEvents();
  CHECK(!scatter_fade->isHidden());  // fade steps appear only for a Fade control
  scatter_fade->setValue(40);
  QApplication::processEvents();
  save_widget_artifact("ui_brush_dynamics_popup_controls", *popup);
  const QPointer<QWidget> closed_popup(popup);
  popup->close();
  // A library reload after the popup is gone but before the ~200ms debounced commit fires (the
  // brush preset library's one-second disk refresh re-resolves the active tip) must not revert
  // the pending edit.
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  CHECK(closed_popup.isNull());
  CHECK(!library.add_tip(QStringLiteral("Reload"), make_bar_tip_image(), 0.5).isEmpty());
  // One debounced commit applies every edit and writes the sidecar in the same slot; wait for
  // it to land, not a fixed sleep.
  CHECK(process_events_until(
      [canvas] { return canvas->brush_dynamics().scatter_fade_steps == 40; }, 5000));

  const auto& dynamics = canvas->brush_dynamics();
  CHECK(dynamics.size_control == patchy::BrushDynamicControl::Off);
  CHECK(dynamics.opacity_control == patchy::BrushDynamicControl::PenPressure);
  CHECK(std::abs(dynamics.minimum_opacity - 0.30) < 1e-9);
  CHECK(dynamics.flow_control == patchy::BrushDynamicControl::Fade);
  CHECK(std::abs(dynamics.minimum_flow - 0.15) < 1e-9);
  CHECK(dynamics.flow_fade_steps == 45);
  CHECK(dynamics.scatter_control == patchy::BrushDynamicControl::Fade);
  CHECK(dynamics.scatter_fade_steps == 40);
  CHECK(dynamics.roundness_control == patchy::BrushDynamicControl::GlobalDefault);  // untouched

  // The sidecar carries the new tokens...
  QFile sidecar(brush_tip_test_storage_dir() + QStringLiteral("/") + tip_id + QStringLiteral(".json"));
  CHECK(sidecar.open(QIODevice::ReadOnly));
  const auto dynamics_json =
      QJsonDocument::fromJson(sidecar.readAll()).object().value(QStringLiteral("dynamics")).toObject();
  sidecar.close();  // Atomic replacement requires readers to release Windows file handles.
  CHECK(dynamics_json.value(QStringLiteral("sizeControl")).toString() == QStringLiteral("off"));
  CHECK(dynamics_json.value(QStringLiteral("opacityControl")).toString() == QStringLiteral("penPressure"));
  CHECK(std::abs(dynamics_json.value(QStringLiteral("minimumOpacity")).toDouble() - 0.30) < 1e-9);
  CHECK(dynamics_json.value(QStringLiteral("scatterControl")).toString() == QStringLiteral("fade"));
  CHECK(dynamics_json.value(QStringLiteral("scatterFadeSteps")).toInt() == 40);
  CHECK(dynamics_json.value(QStringLiteral("flowControl")).toString() == QStringLiteral("fade"));
  CHECK(std::abs(dynamics_json.value(QStringLiteral("minimumFlow")).toDouble() - 0.15) < 1e-9);
  CHECK(dynamics_json.value(QStringLiteral("flowFadeSteps")).toInt() == 45);

  // ...and a fresh library over the same storage round-trips them.
  patchy::ui::BrushTipLibrary second(brush_tip_test_storage_dir());
  const auto* reloaded = second.find_entry(tip_id);
  CHECK(reloaded != nullptr);
  CHECK(reloaded->dynamics.size_control == patchy::BrushDynamicControl::Off);
  CHECK(reloaded->dynamics.opacity_control == patchy::BrushDynamicControl::PenPressure);
  CHECK(std::abs(reloaded->dynamics.minimum_opacity - 0.30) < 1e-9);
  CHECK(reloaded->dynamics.scatter_control == patchy::BrushDynamicControl::Fade);
  CHECK(reloaded->dynamics.scatter_fade_steps == 40);
  CHECK(reloaded->dynamics.flow_control == patchy::BrushDynamicControl::Fade);
  CHECK(std::abs(reloaded->dynamics.minimum_flow - 0.15) < 1e-9);
  CHECK(reloaded->dynamics.flow_fade_steps == 45);

  // An Off-only override is not active() (no per-dab work) but must still light the button
  // badge as a deliberate setup.
  patchy::BrushDynamics off_only;
  off_only.size_control = patchy::BrushDynamicControl::Off;
  CHECK(library.set_tip_dynamics(tip_id, off_only, 0.0, 100.0));
  QApplication::processEvents();
  CHECK(!off_only.active());
  CHECK(button->property("dynamicsActive").toBool());
  clear_brush_tip_test_state();
}

namespace {

QWidget* open_brush_dynamics_popup(patchy::ui::MainWindow& window) {
  auto* button = window.findChild<QToolButton*>(QStringLiteral("brushDynamicsButton"));
  CHECK(button != nullptr);
  if (button == nullptr) {
    return nullptr;
  }
  button->click();
  QApplication::processEvents();
  QWidget* popup = nullptr;
  for (auto* widget : QApplication::topLevelWidgets()) {
    if (widget->objectName() == QStringLiteral("brushDynamicsPopup") && widget->isVisible()) {
      popup = widget;
    }
  }
  CHECK(popup != nullptr);
  return popup;
}

void close_brush_dynamics_popup(QWidget* popup) {
  popup->close();
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  process_events_for(350);  // past the button's dismissal-swallow window
}

}  // namespace

// GitHub issue 506: the popup is a fixed ~520 x 600 with section tabs; no page needs a
// scrollbar at that size, and the last tab comes back on the next opening.
void ui_brush_dynamics_popup_tabs_switch_and_remember() {
  clear_brush_tip_test_state();
  patchy::ui::app_settings().remove(QStringLiteral("brushes/dynamicsSection"));
  patchy::ui::MainWindow window;
  show_window(window);
  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  QApplication::processEvents();
  auto& library = window.brush_tip_library();
  const auto tip_id = library.add_tip(QStringLiteral("Bar"), make_bar_tip_image(), 0.25);
  window.set_active_brush_tip(tip_id, false);

  auto* popup = open_brush_dynamics_popup(window);
  CHECK(popup != nullptr);
  if (popup == nullptr) {
    return;
  }
  CHECK(popup->height() <= 600);
  CHECK(popup->width() >= 520);
  CHECK(popup->width() <= 600);  // about 520; the control combos and labels set the floor
  auto* panel = popup->findChild<patchy::ui::BrushDynamicsPanel*>();
  CHECK(panel != nullptr);
  // Every page fits a 600 px popup: the panel's whole-content height is within it.
  CHECK(panel->sizeHint().height() + 2 * qobject_cast<QFrame*>(popup)->frameWidth() <= 600);
  CHECK(panel->current_section() == patchy::ui::BrushDynamicsPanel::Section::TipShape);
  // The preview strip paints the active tip without touching the document.
  CHECK(panel->preview() != nullptr);
  CHECK(panel->preview()->isVisible());
  panel->preview()->rebuild_now();
  CHECK(!panel->preview()->stroke_image().isNull());

  using Section = patchy::ui::BrushDynamicsPanel::Section;
  const std::array<std::pair<Section, const char*>, 8> sections{{
      {Section::TipShape, "TipShape"},
      {Section::ShapeDynamics, "ShapeDynamics"},
      {Section::Scattering, "Scattering"},
      {Section::Transfer, "Transfer"},
      {Section::Texture, "Texture"},
      {Section::DualBrush, "DualBrush"},
      {Section::ColorDynamics, "ColorDynamics"},
      {Section::BrushEffects, "BrushEffects"},
  }};
  auto* stack = popup->findChild<QStackedWidget*>(QStringLiteral("dynamicsSectionStack"));
  CHECK(stack != nullptr);
  for (const auto& [section, name] : sections) {
    auto* tab = popup->findChild<QPushButton*>(QStringLiteral("dynamicsTab%1").arg(QLatin1String(name)));
    CHECK(tab != nullptr);
    tab->click();
    QApplication::processEvents();
    CHECK(panel->current_section() == section);
    CHECK(tab->isChecked());
    auto* page = qobject_cast<QScrollArea*>(stack->currentWidget());
    CHECK(page != nullptr);
    CHECK(page->objectName() == QStringLiteral("dynamics%1Page").arg(QLatin1String(name)));
    CHECK(!page->verticalScrollBar()->isVisible());
    panel->preview()->rebuild_now();
    save_widget_artifact(
        QStringLiteral("ui_brush_dynamics_popup_tab_%1").arg(QLatin1String(name)).toStdString(), *popup);
  }
  // Switchable sections carry their on/off box in the tab; Tip Shape and Brush Effects do not.
  for (const auto* name : {"ShapeDynamics", "Scattering", "Transfer", "DualBrush"}) {
    auto* row = popup->findChild<QWidget*>(QStringLiteral("dynamicsTab%1Row").arg(QLatin1String(name)));
    CHECK(row != nullptr);
    CHECK(row->findChild<QCheckBox*>() != nullptr);
  }
  CHECK(popup->findChild<QWidget*>(QStringLiteral("dynamicsTabTextureRow"))
            ->findChild<QCheckBox*>(QStringLiteral("dynamicsTextureEnabledCheck")) != nullptr);
  CHECK(popup->findChild<QWidget*>(QStringLiteral("dynamicsTabColorDynamicsRow"))
            ->findChild<QCheckBox*>(QStringLiteral("dynamicsColorEnabledCheck")) != nullptr);
  for (const auto* name : {"TipShape", "BrushEffects"}) {
    auto* row = popup->findChild<QWidget*>(QStringLiteral("dynamicsTab%1Row").arg(QLatin1String(name)));
    CHECK(row != nullptr);
    for (auto* check : row->findChildren<QCheckBox*>()) {
      CHECK(check->isHidden());  // only an alignment stand-in
    }
  }

  // Rows a jitter/control combination makes meaningless are disabled.
  auto* minimum_diameter = popup->findChild<QSpinBox*>(QStringLiteral("dynamicsMinimumDiameterSpin"));
  CHECK(!minimum_diameter->isEnabled());
  popup->findChild<QSpinBox*>(QStringLiteral("dynamicsSizeJitterSpin"))->setValue(30);
  CHECK(minimum_diameter->isEnabled());
  auto* count_jitter = popup->findChild<QSpinBox*>(QStringLiteral("dynamicsCountJitterSpin"));
  CHECK(!count_jitter->isEnabled());
  popup->findChild<QSpinBox*>(QStringLiteral("dynamicsCountSpin"))->setValue(2);
  CHECK(count_jitter->isEnabled());

  panel->select_section(Section::Scattering);
  CHECK(patchy::ui::app_settings().value(QStringLiteral("brushes/dynamicsSection")).toString() ==
        QStringLiteral("scattering"));
  close_brush_dynamics_popup(popup);
  auto* reopened = open_brush_dynamics_popup(window);
  CHECK(reopened != nullptr);
  if (reopened != nullptr) {
    auto* reopened_panel = reopened->findChild<patchy::ui::BrushDynamicsPanel*>();
    CHECK(reopened_panel->current_section() == Section::Scattering);
    close_brush_dynamics_popup(reopened);
  }
  patchy::ui::app_settings().remove(QStringLiteral("brushes/dynamicsSection"));
  clear_brush_tip_test_state();
}

// A section switched off keeps its values (saved with the tip) while the canvas and the
// preview paint without it; switched back on, both return.
void ui_brush_dynamics_section_switch_keeps_values() {
  clear_brush_tip_test_state();
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  QApplication::processEvents();
  auto& library = window.brush_tip_library();
  const auto tip_id = library.add_tip(QStringLiteral("Bar"), make_bar_tip_image(), 0.25);
  window.set_active_brush_tip(tip_id, false);

  auto* popup = open_brush_dynamics_popup(window);
  CHECK(popup != nullptr);
  if (popup == nullptr) {
    return;
  }
  auto* panel = popup->findChild<patchy::ui::BrushDynamicsPanel*>();
  panel->select_section(patchy::ui::BrushDynamicsPanel::Section::Scattering);
  auto* scatter = popup->findChild<QSpinBox*>(QStringLiteral("dynamicsScatterSpin"));
  scatter->setValue(300);
  popup->findChild<QSpinBox*>(QStringLiteral("dynamicsCountSpin"))->setValue(3);
  CHECK(process_events_until([canvas] { return canvas->brush_dynamics().count == 3; }, 5000));
  panel->preview()->rebuild_now();
  const auto with_scatter = panel->preview()->stroke_image();

  auto* enabled = popup->findChild<QCheckBox*>(QStringLiteral("dynamicsScatteringEnabledCheck"));
  CHECK(enabled != nullptr);
  CHECK(enabled->isChecked());
  enabled->setChecked(false);
  QApplication::processEvents();
  CHECK(!scatter->isEnabled());    // the page's rows read as inactive
  CHECK(scatter->value() == 300);  // but keep their values
  panel->preview()->rebuild_now();
  save_widget_artifact("ui_brush_dynamics_popup_scattering_off", *popup);
  const auto without_scatter = panel->preview()->stroke_image();
  CHECK(!without_scatter.isNull());
  CHECK(without_scatter != with_scatter);
  CHECK(process_events_until([canvas] { return canvas->brush_dynamics().count == 1; }, 5000));
  CHECK(canvas->brush_dynamics().scatter == 0.0);
  const auto* entry = library.find_entry(tip_id);
  CHECK(entry != nullptr);
  CHECK(!entry->dynamics.scattering_enabled);
  CHECK(std::abs(entry->dynamics.scatter - 3.0) < 1e-9);
  CHECK(entry->dynamics.count == 3);
  // The sidecar carries the switch under its own key...
  const auto json = patchy::ui::brush_dynamics_to_json(entry->dynamics);
  CHECK(!json.value(QStringLiteral("scatteringEnabled")).toBool(true));
  CHECK(json.value(QStringLiteral("shapeDynamicsEnabled")).toBool(false));
  CHECK(!patchy::ui::brush_dynamics_from_json(json).scattering_enabled);
  // ...and a brush saved before the switches existed loads with every section on.
  const auto legacy = patchy::ui::brush_dynamics_from_json(
      QJsonObject{{QStringLiteral("scatter"), 1.0}, {QStringLiteral("opacityJitter"), 0.5}});
  CHECK(legacy.shape_dynamics_enabled && legacy.scattering_enabled && legacy.transfer_enabled);
  CHECK(legacy.active());

  enabled->setChecked(true);
  QApplication::processEvents();
  CHECK(scatter->isEnabled());
  panel->preview()->rebuild_now();
  CHECK(panel->preview()->stroke_image() == with_scatter);
  CHECK(process_events_until([canvas] { return canvas->brush_dynamics().count == 3; }, 5000));
  CHECK(std::abs(canvas->brush_dynamics().scatter - 3.0) < 1e-9);
  close_brush_dynamics_popup(popup);
  clear_brush_tip_test_state();
}

// Reset Section resets one page; the tab dots mark the sections that differ from defaults.
void ui_brush_dynamics_reset_section_and_default_dots() {
  clear_brush_tip_test_state();
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  QApplication::processEvents();
  auto& library = window.brush_tip_library();
  const auto tip_id = library.add_tip(QStringLiteral("Bar"), make_bar_tip_image(), 0.25);
  window.set_active_brush_tip(tip_id, false);
  auto* popup = open_brush_dynamics_popup(window);
  CHECK(popup != nullptr);
  if (popup == nullptr) {
    return;
  }
  using Section = patchy::ui::BrushDynamicsPanel::Section;
  auto* panel = popup->findChild<patchy::ui::BrushDynamicsPanel*>();
  for (int index = 0; index < patchy::ui::BrushDynamicsPanel::kSectionCount; ++index) {
    CHECK(!panel->section_marked_changed(static_cast<Section>(index)));
  }
  popup->findChild<QSpinBox*>(QStringLiteral("dynamicsSizeJitterSpin"))->setValue(30);
  popup->findChild<QSpinBox*>(QStringLiteral("dynamicsScatterSpin"))->setValue(120);
  popup->findChild<QSpinBox*>(QStringLiteral("dynamicsBaseAngleSpin"))->setValue(45);
  popup->findChild<QCheckBox*>(QStringLiteral("dynamicsWetEdgesCheck"))->setChecked(true);
  CHECK(panel->section_marked_changed(Section::TipShape));
  CHECK(panel->section_marked_changed(Section::ShapeDynamics));
  CHECK(panel->section_marked_changed(Section::Scattering));
  CHECK(panel->section_marked_changed(Section::BrushEffects));
  CHECK(!panel->section_marked_changed(Section::Transfer));
  CHECK(!panel->section_marked_changed(Section::Texture));
  // Switching Texture on alone changes no value: no dot, the box itself shows it is on.
  popup->findChild<QCheckBox*>(QStringLiteral("dynamicsTextureEnabledCheck"))->setChecked(true);
  CHECK(!panel->section_marked_changed(Section::Texture));
  panel->select_section(Section::Scattering);
  QApplication::processEvents();
  panel->preview()->rebuild_now();
  save_widget_artifact("ui_brush_dynamics_popup_changed_dots", *popup);

  auto* stack = popup->findChild<QStackedWidget*>(QStringLiteral("dynamicsSectionStack"));
  auto* reset = stack->currentWidget()->findChild<QPushButton*>(QStringLiteral("dynamicsResetSectionButton"));
  CHECK(reset != nullptr);
  CHECK(reset->isEnabled());
  reset->click();
  QApplication::processEvents();
  CHECK(popup->findChild<QSpinBox*>(QStringLiteral("dynamicsScatterSpin"))->value() == 0);
  CHECK(popup->findChild<QSpinBox*>(QStringLiteral("dynamicsSizeJitterSpin"))->value() == 30);
  CHECK(!panel->section_marked_changed(Section::Scattering));
  CHECK(panel->section_marked_changed(Section::ShapeDynamics));
  CHECK(!reset->isEnabled());  // nothing left to reset on this page
  CHECK(process_events_until(
      [canvas] {
        return canvas->brush_dynamics().scatter == 0.0 && canvas->brush_dynamics().size_jitter > 0.29;
      },
      5000));

  panel->select_section(Section::TipShape);
  stack->currentWidget()->findChild<QPushButton*>(QStringLiteral("dynamicsResetSectionButton"))->click();
  CHECK(popup->findChild<QSpinBox*>(QStringLiteral("dynamicsBaseAngleSpin"))->value() == 0);
  CHECK(!panel->section_marked_changed(Section::TipShape));
  CHECK(panel->section_marked_changed(Section::BrushEffects));

  popup->findChild<QPushButton*>(QStringLiteral("dynamicsResetButton"))->click();
  for (int index = 0; index < patchy::ui::BrushDynamicsPanel::kSectionCount; ++index) {
    CHECK(!panel->section_marked_changed(static_cast<Section>(index)));
  }
  CHECK(!popup->findChild<QCheckBox*>(QStringLiteral("dynamicsTextureEnabledCheck"))->isChecked());
  close_brush_dynamics_popup(popup);
  clear_brush_tip_test_state();
}

namespace {

void clear_brush_window_settings() {
  for (const auto* key : {"brushes/settingsWindowOpen", "brushes/settingsWindowSize", "brushes/tipsWindowOpen",
                          "brushes/tipsWindowSize"}) {
    patchy::ui::app_settings().remove(QLatin1String(key));
  }
}

QWidget* visible_top_level(const char* name) {
  for (auto* widget : QApplication::topLevelWidgets()) {
    if (widget->objectName() == QLatin1String(name) && widget->isVisible()) {
      return widget;
    }
  }
  return nullptr;
}

QWidget* open_tip_picker_popup(patchy::ui::MainWindow& window) {
  auto* picker = window.findChild<patchy::ui::BrushTipPicker*>(QStringLiteral("brushTipPicker"));
  CHECK(picker != nullptr);
  if (picker == nullptr) {
    return nullptr;
  }
  picker->click();
  QApplication::processEvents();
  auto* popup = visible_top_level("brushTipPickerPopup");
  CHECK(popup != nullptr);
  return popup;
}

// The listed tip ids, sorted (the list orders by folder, then name).
QStringList list_tip_ids(QListWidget& list) {
  QStringList ids;
  for (int row = 0; row < list.count(); ++row) {
    ids.push_back(list.item(row)->data(Qt::UserRole).toString());
  }
  ids.sort();
  return ids;
}

QStringList sorted_ids(QStringList ids) {
  ids.sort();
  return ids;
}

// True once a stroke across the middle of the canvas changed what it shows.
bool stroke_paints(patchy::ui::CanvasWidget& canvas) {
  canvas.set_primary_color(QColor(0, 0, 0));
  canvas.set_brush_size(16);
  const auto before = canvas.grab().toImage();
  const auto from = canvas.widget_position_for_document_point(QPoint(60, 60));
  const auto to = canvas.widget_position_for_document_point(QPoint(200, 120));
  drag(canvas, from, to);
  QApplication::processEvents();
  return canvas.grab().toImage() != before;
}

// The window forwards the keys no field of it takes to the canvas.
bool window_forwards_keys_to(QWidget& tool_window, QWidget& canvas) {
  struct Counter : QObject {
    int presses{0};
    bool eventFilter(QObject* /*watched*/, QEvent* event) override {
      if (event->type() == QEvent::KeyPress && static_cast<QKeyEvent*>(event)->key() == Qt::Key_F12) {
        ++presses;
      }
      return false;
    }
  } counter;
  canvas.installEventFilter(&counter);
  QKeyEvent press(QEvent::KeyPress, Qt::Key_F12, Qt::NoModifier);
  QCoreApplication::sendEvent(&tool_window, &press);
  canvas.removeEventFilter(&counter);
  return counter.presses == 1;
}

}  // namespace

// GitHub issue 506 follow-up: the tip chooser's Filter field narrows by name or folder with the
// folder combo, takes the keyboard as the popup opens, Enter picks the first match and Escape
// clears before it closes. The folder is remembered; the text is not.
void ui_brush_tip_picker_filter_narrows_and_picks() {
  clear_brush_tip_test_state();
  patchy::ui::MainWindow window;
  show_window(window);
  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  QApplication::processEvents();
  auto& library = window.brush_tip_library();
  const auto bar_id = library.add_tip(QStringLiteral("Bar"), make_bar_tip_image(), 0.25);
  const auto wide_id = library.add_tip(QStringLiteral("Wide Bar"), make_bar_tip_image(), 0.25, QStringLiteral("Set A"));
  const auto stone_id = library.add_tip(QStringLiteral("Stone"), make_bar_tip_image(), 0.25, QStringLiteral("Set A"));
  CHECK(!bar_id.isEmpty() && !wide_id.isEmpty() && !stone_id.isEmpty());
  QApplication::processEvents();

  auto* popup = open_tip_picker_popup(window);
  if (popup == nullptr) {
    return;
  }
  auto* filter = popup->findChild<QLineEdit*>(QStringLiteral("brushTipPickerFilterEdit"));
  auto* list = popup->findChild<QListWidget*>(QStringLiteral("brushTipPickerList"));
  auto* folders = popup->findChild<QComboBox*>(QStringLiteral("brushTipPickerFolderCombo"));
  CHECK(filter != nullptr && list != nullptr && folders != nullptr);
  if (filter == nullptr || list == nullptr || folders == nullptr) {
    return;
  }
  CHECK(filter->placeholderText() == QObject::tr("Filter"));
  CHECK(filter->isClearButtonEnabled());
  CHECK(filter->hasFocus());  // type at once
  const auto everything = list->count();
  CHECK(everything >= 5);  // Round, Square and the three tips

  QTest::keyClicks(filter, QStringLiteral("BAR"));
  QApplication::processEvents();
  CHECK((list_tip_ids(*list) == sorted_ids({bar_id, wide_id})));  // case-insensitive, by name
  filter->setText(QStringLiteral("set a"));
  CHECK((list_tip_ids(*list) == sorted_ids({wide_id, stone_id})));  // by folder too
  filter->setText(QStringLiteral("bar"));
  folders->setCurrentIndex(folders->findData(QStringLiteral("Set A")));
  CHECK((list_tip_ids(*list) == QStringList{wide_id}));  // with the folder combo
  save_widget_artifact("ui_brush_tip_picker_filter", *popup);

  // Escape clears the text first and keeps the popup open.
  QTest::keyClick(filter, Qt::Key_Escape);
  QApplication::processEvents();
  CHECK(filter->text().isEmpty());
  CHECK(visible_top_level("brushTipPickerPopup") != nullptr);
  CHECK((list_tip_ids(*list) == sorted_ids({wide_id, stone_id})));
  // On an empty field it closes the popup.
  QTest::keyClick(filter, Qt::Key_Escape);
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  QApplication::processEvents();
  CHECK(visible_top_level("brushTipPickerPopup") == nullptr);
  process_events_for(350);

  // The folder comes back, the text does not; Enter picks the first match.
  popup = open_tip_picker_popup(window);
  if (popup == nullptr) {
    return;
  }
  filter = popup->findChild<QLineEdit*>(QStringLiteral("brushTipPickerFilterEdit"));
  folders = popup->findChild<QComboBox*>(QStringLiteral("brushTipPickerFolderCombo"));
  CHECK(filter->text().isEmpty());
  CHECK(folders->currentData().toString() == QStringLiteral("Set A"));
  QTest::keyClicks(filter, QStringLiteral("ston"));
  QTest::keyClick(filter, Qt::Key_Return);
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  QApplication::processEvents();
  CHECK(visible_top_level("brushTipPickerPopup") == nullptr);
  auto* picker = window.findChild<patchy::ui::BrushTipPicker*>(QStringLiteral("brushTipPicker"));
  CHECK(picker->current_tip_id() == stone_id);
  picker->set_folder_filter(QString());
  clear_brush_tip_test_state();
}

// The tip chooser floats: Keep Open turns the popup into a Brush Tips window owned by the main
// window, picks apply to the tool, painting works with it open, it follows a tool or tip change
// made elsewhere, and its size and open state are remembered.
void ui_brush_tips_window_floats_and_follows_the_tool() {
  clear_brush_tip_test_state();
  clear_brush_window_settings();
  const auto cleanup = qScopeGuard([] { clear_brush_window_settings(); });
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  QApplication::processEvents();
  auto& library = window.brush_tip_library();
  const auto bar_id = library.add_tip(QStringLiteral("Bar"), make_bar_tip_image(), 0.25);
  const auto other_id = library.add_tip(QStringLiteral("Other"), make_bar_tip_image(), 0.5);
  QApplication::processEvents();
  auto* picker = window.findChild<patchy::ui::BrushTipPicker*>(QStringLiteral("brushTipPicker"));

  auto* popup = open_tip_picker_popup(window);
  if (popup == nullptr) {
    return;
  }
  const QRect popup_rect(popup->mapToGlobal(QPoint(0, 0)), popup->size());
  auto* keep_open = popup->findChild<QPushButton*>(QStringLiteral("brushTipPickerKeepOpenButton"));
  CHECK(keep_open != nullptr);
  CHECK(!keep_open->isFlat());  // a framed button, not a text label
  keep_open->click();
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  QApplication::processEvents();
  CHECK(visible_top_level("brushTipPickerPopup") == nullptr);
  auto* tips_window = picker->tool_window();
  CHECK(tips_window != nullptr && tips_window->isVisible());
  if (tips_window == nullptr) {
    return;
  }
  CHECK(tips_window->objectName() == QStringLiteral("brushTipsWindow"));
  CHECK(tips_window->windowFlags().testFlag(Qt::Tool));
  CHECK(tips_window->parentWidget() == &window);  // owned: above the main window, closes with it
  CHECK(!tips_window->isModal());
  // Where the popup was (kept on the screen).
  CHECK(tips_window->frameGeometry().intersects(popup_rect));
  CHECK(patchy::ui::app_settings().value(QStringLiteral("brushes/tipsWindowOpen")).toBool());

  // A click picks the tip and the window stays.
  auto* list = tips_window->findChild<QListWidget*>(QStringLiteral("brushTipPickerList"));
  CHECK(list != nullptr);
  for (int row = 0; row < list->count(); ++row) {
    if (list->item(row)->data(Qt::UserRole).toString() == bar_id) {
      list->setCurrentRow(row);
      emit list->itemClicked(list->item(row));
    }
  }
  QApplication::processEvents();
  CHECK(picker->current_tip_id() == bar_id);
  CHECK(canvas->has_brush_tip());
  CHECK(tips_window->isVisible());
  // Painting works with the window open, and keys reach the canvas.
  CHECK(stroke_paints(*canvas));
  CHECK(window_forwards_keys_to(*tips_window, *canvas));

  // A tip chosen elsewhere moves the window's mark.
  window.set_active_brush_tip(other_id, false);
  QApplication::processEvents();
  CHECK(list->currentItem() != nullptr && list->currentItem()->data(Qt::UserRole).toString() == other_id);
  // A tool without a tip leaves the window disabled with the reason; a tool with one wakes it.
  auto* reason = tips_window->findChild<QLabel*>(QStringLiteral("brushToolWindowUnavailableLabel"));
  CHECK(reason != nullptr);
  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  QApplication::processEvents();
  CHECK(!reason->isHidden());
  CHECK(!list->isEnabled());
  require_action_by_text(window, QStringLiteral("Eraser"))->trigger();
  QApplication::processEvents();
  CHECK(reason->isHidden());
  CHECK(list->isEnabled());
  save_widget_artifact("ui_brush_tips_window", *tips_window);

  // The size and the open state are remembered; the picker raises the window instead of a popup.
  tips_window->resize(520, 430);
  QApplication::processEvents();
  picker->click();
  QApplication::processEvents();
  CHECK(visible_top_level("brushTipPickerPopup") == nullptr);
  tips_window->close();
  QApplication::processEvents();
  CHECK(!tips_window->isVisible());
  CHECK(!patchy::ui::app_settings().value(QStringLiteral("brushes/tipsWindowOpen")).toBool());
  CHECK(patchy::ui::app_settings().value(QStringLiteral("brushes/tipsWindowSize")).toSize() == QSize(520, 430));
  require_action(window, "windowBrushTipsAction")->trigger();
  CHECK(process_events_until([&] { return tips_window->isVisible(); }));
  CHECK(tips_window->size() == QSize(520, 430));
  tips_window->close();
  QApplication::processEvents();
  clear_brush_tip_test_state();
}

// The Dynamics panel floats: Keep Open turns the popup into the Brush Dynamics and Effects window, its edits
// reach the canvas live, a tip switch or an outside change reloads it, it follows the tool, and
// the size and open state are remembered.
void ui_brush_settings_window_floats_and_edits_live() {
  clear_brush_tip_test_state();
  clear_brush_window_settings();
  const auto cleanup = qScopeGuard([] { clear_brush_window_settings(); });
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  QApplication::processEvents();
  auto& library = window.brush_tip_library();
  const auto bar_id = library.add_tip(QStringLiteral("Bar"), make_bar_tip_image(), 0.25);
  const auto other_id = library.add_tip(QStringLiteral("Other"), make_bar_tip_image(), 0.25);
  patchy::BrushDynamics other_dynamics;
  other_dynamics.scatter = 1.5;
  CHECK(library.set_tip_dynamics(other_id, other_dynamics, 0.0, 100.0));
  window.set_active_brush_tip(bar_id, false);
  QApplication::processEvents();

  auto* button = window.findChild<patchy::ui::BrushDynamicsButton*>(QStringLiteral("brushDynamicsButton"));
  CHECK(button != nullptr);
  if (button == nullptr) {
    return;
  }
  button->click();
  QApplication::processEvents();
  auto* popup = visible_top_level("brushDynamicsPopup");
  CHECK(popup != nullptr);
  if (popup == nullptr) {
    return;
  }
  popup->findChild<patchy::ui::BrushDynamicsPanel*>()->select_section(
      patchy::ui::BrushDynamicsPanel::Section::Scattering);
  CHECK(popup->height() <= 600);
  auto* keep_open = popup->findChild<QPushButton*>(QStringLiteral("brushDynamicsKeepOpenButton"));
  CHECK(keep_open != nullptr);
  CHECK(!keep_open->isFlat());
  QApplication::processEvents();
  save_widget_artifact("ui_brush_dynamics_popup_keep_open", *popup);
  keep_open->click();
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  QApplication::processEvents();
  CHECK(visible_top_level("brushDynamicsPopup") == nullptr);
  auto* settings_window = button->tool_window();
  auto* panel = button->window_panel();
  CHECK(settings_window != nullptr && settings_window->isVisible() && panel != nullptr);
  if (settings_window == nullptr || panel == nullptr) {
    return;
  }
  CHECK(settings_window->windowFlags().testFlag(Qt::Tool));
  CHECK(settings_window->parentWidget() == &window);
  CHECK(panel->current_section() == patchy::ui::BrushDynamicsPanel::Section::Scattering);
  CHECK(button->isChecked());  // the button reads as pressed while its window is open

  // Edits apply live while the window stays open.
  auto* scatter = settings_window->findChild<QSpinBox*>(QStringLiteral("dynamicsScatterSpin"));
  CHECK(scatter != nullptr);
  scatter->setValue(250);
  CHECK(process_events_until([canvas] { return std::abs(canvas->brush_dynamics().scatter - 2.5) < 1e-9; }, 5000));
  CHECK(settings_window->isVisible());
  CHECK(stroke_paints(*canvas));
  CHECK(window_forwards_keys_to(*settings_window, *canvas));
  // The button raises the window instead of opening a second panel.
  button->click();
  QApplication::processEvents();
  CHECK(visible_top_level("brushDynamicsPopup") == nullptr);

  // Another tip reloads the window; so does a change made outside it (a script, a preset).
  window.set_active_brush_tip(other_id, false);
  QApplication::processEvents();
  CHECK(scatter->value() == 150);
  patchy::BrushDynamics changed;
  changed.scatter = 0.4;
  CHECK(library.set_tip_dynamics(other_id, changed, 0.0, 100.0));
  QApplication::processEvents();
  CHECK(process_events_until([scatter] { return scatter->value() == 40; }, 5000));

  // The Eraser has no dynamics: the window says so and its panel is disabled.
  auto* reason = settings_window->findChild<QLabel*>(QStringLiteral("brushToolWindowUnavailableLabel"));
  require_action_by_text(window, QStringLiteral("Eraser"))->trigger();
  QApplication::processEvents();
  CHECK(!reason->isHidden());
  CHECK(!panel->isEnabled());
  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  QApplication::processEvents();
  CHECK(reason->isHidden());
  CHECK(panel->isEnabled());
  panel->preview()->rebuild_now();
  save_widget_artifact("ui_brush_settings_window", *settings_window);

  settings_window->resize(600, 640);
  QApplication::processEvents();
  settings_window->close();
  QApplication::processEvents();
  CHECK(!button->isChecked());
  CHECK(!patchy::ui::app_settings().value(QStringLiteral("brushes/settingsWindowOpen")).toBool());
  CHECK(patchy::ui::app_settings().value(QStringLiteral("brushes/settingsWindowSize")).toSize() ==
        QSize(600, 640));
  require_action(window, "windowBrushSettingsAction")->trigger();
  CHECK(process_events_until([&] { return settings_window->isVisible(); }));
  CHECK(settings_window->size() == QSize(600, 640));
  settings_window->close();
  QApplication::processEvents();
  clear_brush_tip_test_state();
}

// Both windows open beside a canvas with a stroke painted (the card's capture).
// Both popups' Keep Open buttons side by side (the card's capture, GitHub issue 506).
void shot_brush_keep_open_buttons() {
  clear_brush_tip_test_state();
  clear_brush_window_settings();
  const auto cleanup = qScopeGuard([] { clear_brush_window_settings(); });
  patchy::ui::MainWindow window;
  show_window(window);
  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  QApplication::processEvents();
  auto& library = window.brush_tip_library();
  const auto bar_id = library.add_tip(QStringLiteral("Bar"), make_bar_tip_image(), 0.25);
  window.set_active_brush_tip(bar_id, false);
  QApplication::processEvents();
  QImage tips_image;
  if (auto* popup = open_tip_picker_popup(window); popup != nullptr) {
    popup->resize(420, 300);
    QApplication::processEvents();
    CHECK(popup->findChild<QPushButton*>(QStringLiteral("brushTipPickerKeepOpenButton")) != nullptr);
    tips_image = popup->grab().toImage();
    popup->close();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    process_events_for(350);
  }
  QImage dynamics_image;
  auto* button = window.findChild<patchy::ui::BrushDynamicsButton*>(QStringLiteral("brushDynamicsButton"));
  CHECK(button != nullptr);
  if (button != nullptr) {
    button->click();
    QApplication::processEvents();
    if (auto* popup = visible_top_level("brushDynamicsPopup"); popup != nullptr) {
      popup->findChild<patchy::ui::BrushDynamicsPanel*>()->select_section(
          patchy::ui::BrushDynamicsPanel::Section::ShapeDynamics);
      QApplication::processEvents();
      popup->findChild<patchy::ui::BrushDynamicsPanel*>()->preview()->rebuild_now();
      QApplication::processEvents();
      dynamics_image = popup->grab().toImage();
      popup->close();
      QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
      QApplication::processEvents();
    }
  }
  CHECK(!tips_image.isNull() && !dynamics_image.isNull());
  if (tips_image.isNull() || dynamics_image.isNull()) {
    return;
  }
  QImage shot(tips_image.width() + dynamics_image.width() + 36,
              std::max(tips_image.height(), dynamics_image.height()) + 24, QImage::Format_ARGB32);
  shot.fill(patchy::ui::theme().window_bg);
  {
    QPainter painter(&shot);
    painter.drawImage(QPoint(12, 12), tips_image);
    painter.drawImage(QPoint(tips_image.width() + 24, 12), dynamics_image);
  }
  save_image_artifact("shot_brush_keep_open_buttons", shot);
  clear_brush_tip_test_state();
}

void shot_brush_floating_windows() {
  clear_brush_tip_test_state();
  clear_brush_window_settings();
  const auto cleanup = qScopeGuard([] { clear_brush_window_settings(); });
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  QApplication::processEvents();
  auto& library = window.brush_tip_library();
  const auto bar_id = library.add_tip(QStringLiteral("Bar"), make_bar_tip_image(), 0.25);
  patchy::BrushDynamics dynamics;
  dynamics.scatter = 1.2;
  dynamics.count = 2;
  dynamics.size_jitter = 0.4;
  dynamics.angle_jitter = 0.3;
  CHECK(library.set_tip_dynamics(bar_id, dynamics, 0.0, 100.0));
  window.set_active_brush_tip(bar_id, false);
  QApplication::processEvents();
  canvas->set_brush_dynamics_test_seed(7);
  CHECK(stroke_paints(*canvas));
  canvas->set_brush_dynamics_test_seed(std::nullopt);
  require_action(window, "windowBrushTipsAction")->trigger();
  require_action(window, "windowBrushSettingsAction")->trigger();
  auto* picker = window.findChild<patchy::ui::BrushTipPicker*>(QStringLiteral("brushTipPicker"));
  auto* button = window.findChild<patchy::ui::BrushDynamicsButton*>(QStringLiteral("brushDynamicsButton"));
  CHECK(process_events_until([&] {
    return picker->tool_window() != nullptr && picker->tool_window()->isVisible() &&
           button->tool_window() != nullptr && button->tool_window()->isVisible();
  }));
  if (picker->tool_window() == nullptr || button->tool_window() == nullptr) {
    return;
  }
  auto* tips_window = picker->tool_window();
  auto* settings_window = button->tool_window();
  tips_window->resize(380, 300);
  button->window_panel()->select_section(patchy::ui::BrushDynamicsPanel::Section::Scattering);
  button->window_panel()->preview()->rebuild_now();
  QApplication::processEvents();
  // The windows over the right of the main window, as a user would park them.
  auto shot = window.grab().toImage();
  {
    QPainter painter(&shot);
    const auto tips_image = tips_window->grab().toImage();
    const auto settings_image = settings_window->grab().toImage();
    const QPoint settings_at(shot.width() - settings_image.width() - 12, 90);
    const QPoint tips_at(settings_at.x() - tips_image.width() - 12, 90);
    painter.drawImage(tips_at, tips_image);
    painter.drawImage(settings_at, settings_image);
  }
  save_image_artifact("shot_brush_floating_windows", shot);
  tips_window->close();
  settings_window->close();
  QApplication::processEvents();
  clear_brush_tip_test_state();
}

void ui_brush_dynamics_button_toggles_popup_closed() {
  clear_brush_tip_test_state();
  patchy::ui::MainWindow window;
  show_window(window);
  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  QApplication::processEvents();
  auto& library = window.brush_tip_library();
  const auto tip_id = library.add_tip(QStringLiteral("Bar"), make_bar_tip_image(), 0.25);
  window.set_active_brush_tip(tip_id, false);
  auto* button = window.findChild<QToolButton*>(QStringLiteral("brushDynamicsButton"));
  CHECK(button != nullptr);

  const auto find_popup = []() -> QWidget* {
    QWidget* popup = nullptr;
    for (auto* widget : QApplication::topLevelWidgets()) {
      if (widget->objectName() == QStringLiteral("brushDynamicsPopup") && widget->isVisible()) {
        popup = widget;
      }
    }
    return popup;
  };

  // An icon button, not the word (GitHub issue 364 follow-up); "Dynamics" stays its
  // accessible name and the tooltip says what it opens.
  CHECK(button->toolButtonStyle() == Qt::ToolButtonIconOnly);
  CHECK(!button->icon().isNull());
  CHECK(button->accessibleName() == QObject::tr("Dynamics"));
  CHECK(button->toolTip().contains(QStringLiteral("dynamics"), Qt::CaseInsensitive));
  CHECK(!button->isChecked());

  button->click();
  QApplication::processEvents();
  CHECK(find_popup() != nullptr);
  CHECK(button->isChecked());  // reads as pressed while its popup is open

  // A second click while the popup is open (the replayed dismissal click) must close it and
  // NOT immediately reopen it — the close-then-instant-reopen was the July 2026 toggle bug.
  button->click();
  QApplication::processEvents();
  CHECK(find_popup() == nullptr);
  CHECK(!button->isChecked());

  // And the button must not stay dead: a later click opens the popup again.
  process_events_for(350);
  button->click();
  QApplication::processEvents();
  auto* reopened = find_popup();
  CHECK(reopened != nullptr);
  CHECK(button->isChecked());
  reopened->close();
  QApplication::processEvents();
  CHECK(!button->isChecked());
  clear_brush_tip_test_state();
}

// GitHub issue 364 follow-up: the Tip picker (and the Brush tool's Dynamics button) lead the
// bar, right after the Preset menu, on every tool that has a tip; Pattern Stamp has no Preset
// and starts with Tip. A separator then sets them apart from Size.
void ui_brush_tip_follows_the_preset_menu() {
  clear_brush_tip_test_state();
  patchy::ui::MainWindow window;
  window.resize(1366, 738);
  show_window(window);
  window.resize(1366, 738);
  auto* options = window.findChild<QWidget*>(QStringLiteral("OptionsContent"));
  CHECK(options != nullptr);
  if (options == nullptr) {
    return;
  }
  const auto row = [options] {
    std::vector<QWidget*> visible;
    for (auto* child : options->findChildren<QWidget*>(Qt::FindDirectChildrenOnly)) {
      if (child->isVisible()) {
        visible.push_back(child);
      }
    }
    std::sort(visible.begin(), visible.end(), [](QWidget* a, QWidget* b) { return a->x() < b->x(); });
    QStringList tokens;
    for (auto* child : visible) {
      const auto* label = qobject_cast<QLabel*>(child);
      tokens << (label != nullptr ? label->text()
                 : child->objectName().isEmpty() ? QString::fromLatin1(child->metaObject()->className())
                                                 : child->objectName());
    }
    return tokens;
  };
  const auto separator = QStringLiteral("optionSeparator");
  struct Case {
    const char* action;
    QStringList lead;
  };
  const auto tip = QStringLiteral("Tip");
  const auto preset = QStringLiteral("Preset");
  const std::vector<Case> cases = {
      {"toolBrushAction", {preset, QStringLiteral("brushPresetCombo"), tip, QStringLiteral("brushTipPicker"),
                           QStringLiteral("brushDynamicsButton"), separator}},
      {"toolEraserAction",
       {preset, QStringLiteral("brushPresetCombo"), tip, QStringLiteral("brushTipPicker"), separator}},
      {"toolMixerBrushAction",
       {preset, QStringLiteral("brushPresetCombo"), tip, QStringLiteral("brushTipPicker"), separator}},
      {"toolPatternStampAction", {tip, QStringLiteral("brushTipPicker"), separator}},
  };
  for (const auto& test_case : cases) {
    require_action(window, test_case.action)->trigger();
    QApplication::processEvents();
    process_events_for(30);
    const auto tokens = row();
    std::printf("  %-24s %s\n", test_case.action, qPrintable(tokens.join(QLatin1Char(' '))));
    CHECK(tokens.mid(0, test_case.lead.size()) == test_case.lead);
    // The tip sits on the first row, ahead of Size.
    auto* picker = window.findChild<patchy::ui::BrushTipPicker*>(QStringLiteral("brushTipPicker"));
    auto* size = window.findChild<QSpinBox*>(QStringLiteral("brushSizeSpin"));
    CHECK(picker != nullptr && size != nullptr);
    if (picker != nullptr && size != nullptr) {
      CHECK(picker->x() < size->x());
      CHECK(std::abs(picker->geometry().center().y() - size->geometry().center().y()) < 8);
    }
  }
  auto* options_bar = window.findChild<QToolBar*>(QStringLiteral("Options"));
  require_action(window, "toolBrushAction")->trigger();
  QApplication::processEvents();
  process_events_for(30);
  if (options_bar != nullptr) {
    save_widget_artifact("ui_brush_options_bar_tip_first", *options_bar);
  }
  clear_brush_tip_test_state();
}

void ui_brush_dynamics_stroke_scatters_with_seed() {
  clear_brush_tip_test_state();
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  QApplication::processEvents();

  auto& library = window.brush_tip_library();
  const auto tip_id = library.add_tip(QStringLiteral("Bar"), make_bar_tip_image(), 2.0);  // sparse dabs
  CHECK(!tip_id.isEmpty());
  window.set_active_brush_tip(tip_id, false);
  canvas->set_primary_color(QColor(0, 0, 0));
  canvas->set_brush_size(16);
  canvas->set_brush_softness(0);

  patchy::BrushDynamics dynamics;
  dynamics.scatter = 1.5;  // up to 24px perpendicular offsets at size 16
  dynamics.count = 3;
  canvas->set_brush_dynamics(dynamics);
  canvas->set_brush_dynamics_test_seed(42);

  const auto from = canvas->widget_position_for_document_point(QPoint(300, 200));
  const auto to = canvas->widget_position_for_document_point(QPoint(500, 200));
  drag(*canvas, from, to);
  QApplication::processEvents();

  // Scattered dabs must land clearly off the stroke line (the bar tip alone spans y 198-202).
  const auto image = canvas->grab().toImage().convertToFormat(QImage::Format_RGB32);
  int off_line_dark = 0;
  for (int y = 160; y <= 240; y += 2) {
    if (std::abs(y - 200) < 10) {
      continue;
    }
    for (int x = 280; x <= 520; x += 2) {
      const auto widget_point = canvas->widget_position_for_document_point(QPoint(x, y));
      if (!image.rect().contains(widget_point)) {
        continue;
      }
      if (QColor(image.pixel(widget_point)).lightness() < 100) {
        ++off_line_dark;
      }
    }
  }
  CHECK(off_line_dark > 5);
  save_widget_artifact("ui_brush_dynamics_scatter_stroke", *canvas);
  canvas->set_brush_dynamics_test_seed(std::nullopt);
  clear_brush_tip_test_state();
}

// #221: with a Direction-controlled tip the press dab waits for the stroke's direction, so a
// canvas stroke's first dab follows it like the rest (with or without Smoothing); a click with
// no movement keeps the base angle. The bar tip is horizontal at angle 0.
void ui_brush_direction_first_dab_follows_stroke() {
  clear_brush_tip_test_state();
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  QApplication::processEvents();
  canvas->set_zoom(1.0);

  auto& library = window.brush_tip_library();
  const auto tip_id = library.add_tip(QStringLiteral("Bar"), make_bar_tip_image(), 1.5);
  CHECK(!tip_id.isEmpty());
  window.set_active_brush_tip(tip_id, false);
  canvas->set_primary_color(QColor(0, 0, 0));
  canvas->set_brush_size(24);  // a 24x6 bar, dabs every 36px
  canvas->set_brush_softness(0);
  canvas->set_brush_opacity(100);
  canvas->set_brush_flow(100);
  patchy::BrushDynamics dynamics;
  dynamics.angle_control = patchy::BrushDynamicControl::Direction;
  canvas->set_brush_dynamics(dynamics);

  canvas->set_brush_smoothing(0);
  drag_document_path(*canvas, {QPoint(200, 130), QPoint(200, 310)}, 45);  // 4px moves
  canvas->set_brush_smoothing(25);
  drag_document_path(*canvas, {QPoint(260, 200), QPoint(440, 200)}, 45);
  canvas->set_brush_smoothing(0);
  const auto click = canvas->widget_position_for_document_point(QPoint(500, 280));
  send_mouse(*canvas, QEvent::MouseButtonPress, click, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, click, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();

  const auto image = canvas->grab().toImage().convertToFormat(QImage::Format_RGB32);
  const auto top_left = canvas->widget_position_for_document_point(QPoint(160, 100));
  const auto bottom_right = canvas->widget_position_for_document_point(QPoint(540, 330));
  save_image_artifact("ui_brush_direction_first_dab", image.copy(QRect(top_left, bottom_right)));
  const auto dark = [&](int x, int y) {
    const auto point = canvas->widget_position_for_document_point(QPoint(x, y));
    return image.rect().contains(point) && QColor(image.pixel(point)).lightness() < 100;
  };

  // Vertical stroke: the first dab at (200,130) is a vertical bar, with no horizontal stamp.
  CHECK(dark(200, 122));
  CHECK(!dark(209, 130));
  CHECK(!dark(191, 130));
  // Horizontal stroke (Smoothing on): the first dab at (260,200) stays horizontal.
  CHECK(dark(268, 200));
  CHECK(!dark(260, 191));
  CHECK(!dark(260, 209));
  // Click: one dab at the base angle.
  CHECK(dark(489, 280));
  CHECK(dark(511, 280));
  CHECK(!dark(500, 271));
  clear_brush_tip_test_state();
}

void ui_brush_color_dynamics_reaches_stroke_compositor() {
  clear_brush_tip_test_state();
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  QApplication::processEvents();

  QImage solid(16, 16, QImage::Format_Grayscale8);
  solid.fill(255);
  auto& library = window.brush_tip_library();
  const auto tip_id = library.add_tip(QStringLiteral("Color Dynamics Probe"), solid, 1.5);
  CHECK(!tip_id.isEmpty());
  window.set_active_brush_tip(tip_id, false);
  canvas->set_primary_color(QColor(255, 0, 0));
  canvas->set_secondary_color(QColor(0, 0, 255));
  canvas->set_brush_size(16);
  canvas->set_brush_softness(0);
  patchy::BrushDynamics dynamics;
  dynamics.color_dynamics_enabled = true;
  dynamics.foreground_background_jitter = 1.0;
  canvas->set_brush_dynamics(dynamics);
  canvas->set_brush_dynamics_test_seed(0xC0104U);

  const auto from = canvas->widget_position_for_document_point(QPoint(300, 200));
  const auto to = canvas->widget_position_for_document_point(QPoint(500, 200));
  drag(*canvas, from, to);
  QApplication::processEvents();

  const auto image = canvas->grab().toImage().convertToFormat(QImage::Format_RGB32);
  int mixed_pixels = 0;
  for (int y = 185; y <= 215; ++y) {
    for (int x = 285; x <= 515; ++x) {
      const auto point = canvas->widget_position_for_document_point(QPoint(x, y));
      if (!image.rect().contains(point)) {
        continue;
      }
      const auto color = QColor(image.pixel(point));
      if (color.red() > 20 && color.blue() > 20 && color.green() < 40) {
        ++mixed_pixels;
      }
    }
  }
  CHECK(mixed_pixels > 100);  // not the captured all-red stroke-start color
  save_widget_artifact("ui_brush_color_dynamics_stroke", *canvas);
  canvas->set_brush_dynamics_test_seed(std::nullopt);
  clear_brush_tip_test_state();
}

void ui_brush_dynamics_abr_import_carries_dynamics() {
  clear_brush_tip_test_state();
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  QApplication::processEvents();

  auto& library = window.brush_tip_library();
  QString error;
  QStringList warnings;
  const auto fixture =
      QString::fromStdString(patchy::test::committed_abr_fixture_path("photoshop-dynamics.abr").string());
  const auto first_id = library.import_abr(fixture, error, warnings);
  CHECK(!first_id.isEmpty());
  CHECK(error.isEmpty());
  CHECK(warnings.isEmpty());
  const auto* entry = library.find_entry(first_id);
  CHECK(entry != nullptr);
  CHECK(entry->dynamics.active());
  CHECK(std::abs(entry->dynamics.size_jitter - 0.37) < 1e-9);
  CHECK(entry->dynamics.angle_control == patchy::BrushDynamicControl::Direction);
  // Photoshop "Off" (no control chosen) imports as follow-the-global-preferences for the
  // aspects the global pen settings cover, plain Off elsewhere.
  CHECK(entry->dynamics.size_control == patchy::BrushDynamicControl::GlobalDefault);
  CHECK(entry->dynamics.opacity_control == patchy::BrushDynamicControl::GlobalDefault);
  CHECK(entry->dynamics.flow_control == patchy::BrushDynamicControl::Off);
  CHECK(entry->dynamics.scatter_control == patchy::BrushDynamicControl::Off);
  CHECK(std::abs(entry->base_angle_degrees - 30.0) < 1e-9);
  CHECK(std::abs(entry->base_roundness - 60.0) < 1e-9);
  CHECK(entry->tool_flow_percent == 100);
  CHECK(entry->tool_airbrush == false);
  patchy::ui::BrushTipLibrary reloaded_library(brush_tip_test_storage_dir());
  const auto* reloaded_entry = reloaded_library.find_entry(first_id);
  CHECK(reloaded_entry != nullptr);
  CHECK(reloaded_entry->tool_flow_percent == 100);
  CHECK(reloaded_entry->tool_airbrush == false);

  // Selecting the tip pushes its dynamics, base shape, and included tool settings to the canvas.
  auto* flow = window.findChild<QSpinBox*>(QStringLiteral("brushFlowSpin"));
  auto* airbrush = window.findChild<QCheckBox*>(QStringLiteral("brushAirbrushCheck"));
  CHECK(flow != nullptr);
  CHECK(airbrush != nullptr);
  flow->setValue(23);
  airbrush->setChecked(true);
  window.set_active_brush_tip(first_id, false);
  CHECK(flow->value() == 100);
  CHECK(!airbrush->isChecked());
  CHECK(canvas->brush_flow() == 100);
  CHECK(!canvas->brush_build_up());
  CHECK(std::abs(canvas->brush_dynamics().scatter - 2.50) < 1e-9);
  CHECK(canvas->brush_dynamics().count == 4);
  CHECK(canvas->brush_base_roundness() == 60);
  auto* button = window.findChild<QToolButton*>(QStringLiteral("brushDynamicsButton"));
  CHECK(button != nullptr);
  CHECK(button->isEnabled());
  CHECK(button->property("dynamicsActive").toBool());

  // A sidecar/library refresh re-applies the tip without repeatedly forcing its imported tool
  // settings over later user edits.
  flow->setValue(41);
  airbrush->setChecked(true);
  CHECK(library.set_tip_dynamics(first_id, entry->dynamics, entry->base_angle_degrees,
                                 entry->base_roundness));
  QApplication::processEvents();
  CHECK(flow->value() == 41);
  CHECK(airbrush->isChecked());

  // Back to Round: dynamics reset with the tip.
  window.set_active_brush_tip(patchy::ui::builtin_round_brush_tip_id(), false);
  CHECK(!canvas->brush_dynamics().active());
  CHECK(canvas->brush_base_roundness() == 100);
  clear_brush_tip_test_state();
}

void ui_brush_tip_soft_stamps_accumulate_without_seams() {
  clear_brush_tip_test_state();
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);

  // A solid square tip at 100% spacing stamps edge-to-edge like the brick/pattern defaults.
  // With a soft tip, adjacent stamps' feathered edges meet on the stroke's center line; the
  // overlap must accumulate toward solid instead of dipping to a light seam between stamps.
  auto& library = window.brush_tip_library();
  const auto square = [] {
    QImage mask(16, 16, QImage::Format_Grayscale8);
    mask.fill(255);
    return mask;
  }();
  const auto tip_id = library.add_tip(QStringLiteral("Seam Square"), square, 1.0);
  CHECK(!tip_id.isEmpty());
  window.set_active_brush_tip(tip_id, false);
  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  canvas->set_primary_color(QColor(0, 0, 0));
  canvas->set_brush_size(32);
  canvas->set_brush_softness(100);

  const auto start = QPoint(60, 100);
  const auto end = QPoint(188, 100);  // five stamps at 32px spacing: x = 60,92,124,156,188
  drag(*canvas, canvas->widget_position_for_document_point(start),
       canvas->widget_position_for_document_point(end));
  QApplication::processEvents();

  const auto image = canvas->grab().toImage().convertToFormat(QImage::Format_RGB32);
  const auto lightness_at = [canvas, &image](QPoint document_point) {
    return QColor(image.pixel(canvas->widget_position_for_document_point(document_point))).lightness();
  };

  CHECK(lightness_at(start) < 60);  // stamp centers are solid
  int max_lightness = 0;
  for (int x = start.x(); x <= end.x(); ++x) {
    max_lightness = std::max(max_lightness, lightness_at(QPoint(x, start.y())));
  }
  // Under the old per-pixel max coverage cap the inter-stamp seams stayed near 50% coverage
  // (lightness ~128); accumulation keeps the whole center line clearly dark.
  CHECK(max_lightness < 110);
  save_widget_artifact("ui_brush_tip_soft_stamp_seams", *canvas);
  clear_brush_tip_test_state();
}

void ui_brush_outline_overlay_tracks_large_brushes() {
  clear_brush_tip_test_state();
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  require_action_by_text(window, QStringLiteral("Brush"))->trigger();

  // Large procedural brush: the OS cursor gives way to a crosshair + canvas overlay.
  canvas->set_brush_size(400);
  CHECK(canvas->cursor().shape() == Qt::CrossCursor);

  const QPoint hover(canvas->width() / 2, canvas->height() / 2);
  send_mouse(*canvas, QEvent::MouseMove, hover, Qt::NoButton, Qt::NoButton);
  QApplication::processEvents();
  const auto image = canvas->grab().toImage().convertToFormat(QImage::Format_RGB32);
  // The outline ring must appear far beyond the old 160px cursor cap. The 1px stroke is
  // antialiased over white, so probe a band around the radius for clearly-darkened pixels.
  const auto display_radius = static_cast<int>(std::round(400.0 * canvas->zoom() / 2.0));
  CHECK(display_radius > 100);
  int dark_ring_pixels = 0;
  for (int angle_step = 0; angle_step < 360; angle_step += 5) {
    const auto radians = angle_step * 3.14159265358979323846 / 180.0;
    for (int probe = -3; probe <= 3; ++probe) {
      const QPoint point(hover.x() + static_cast<int>(std::round((display_radius + probe) * std::cos(radians))),
                         hover.y() + static_cast<int>(std::round((display_radius + probe) * std::sin(radians))));
      if (image.rect().contains(point) && QColor(image.pixel(point)).lightness() < 200) {
        ++dark_ring_pixels;
        break;
      }
    }
  }
  save_widget_artifact("ui_brush_outline_overlay_large", *canvas);
  CHECK(dark_ring_pixels > 20);

  // Shrinking the brush returns to a pixmap cursor (no overlay crosshair).
  canvas->set_brush_size(48);
  CHECK(canvas->cursor().shape() != Qt::CrossCursor);
  clear_brush_tip_test_state();
}

void ui_clone_outline_overlay_tracks_large_brushes() {
  clear_brush_tip_test_state();
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  require_action_by_text(window, QStringLiteral("Clone"))->trigger();
  // Without a source the not-allowed cursor shows (GitHub issue 380): Alt-click one first.
  const QPoint source(canvas->width() / 2, canvas->height() / 2);
  send_mouse(*canvas, QEvent::MouseButtonPress, source, Qt::LeftButton, Qt::LeftButton, Qt::AltModifier);
  send_mouse(*canvas, QEvent::MouseButtonRelease, source, Qt::LeftButton, Qt::NoButton, Qt::AltModifier);
  send_mouse(*canvas, QEvent::MouseMove, source, Qt::NoButton, Qt::NoButton);
  QApplication::processEvents();

  // Large Clone footprint: the same crosshair + canvas-overlay outline as the
  // Brush (the unbounded pixmap cursor was rejected by browsers on wasm).
  canvas->set_brush_size(400);
  CHECK(canvas->cursor().shape() == Qt::CrossCursor);

  const QPoint hover(canvas->width() / 2, canvas->height() / 2);
  const auto display_radius = static_cast<int>(std::round(400.0 * canvas->zoom() / 2.0));
  CHECK(display_radius > 100);
  const auto count_dark_ring_pixels = [&]() {
    send_mouse(*canvas, QEvent::MouseMove, hover, Qt::NoButton, Qt::NoButton);
    QApplication::processEvents();
    const auto image = canvas->grab().toImage().convertToFormat(QImage::Format_RGB32);
    int dark_ring_pixels = 0;
    for (int angle_step = 0; angle_step < 360; angle_step += 5) {
      const auto radians = angle_step * 3.14159265358979323846 / 180.0;
      for (int probe = -3; probe <= 3; ++probe) {
        const QPoint point(
            hover.x() + static_cast<int>(std::round((display_radius + probe) * std::cos(radians))),
            hover.y() + static_cast<int>(std::round((display_radius + probe) * std::sin(radians))));
        if (image.rect().contains(point) && QColor(image.pixel(point)).lightness() < 200) {
          ++dark_ring_pixels;
          break;
        }
      }
    }
    return dark_ring_pixels;
  };
  CHECK(count_dark_ring_pixels() > 20);
  save_widget_artifact("ui_clone_outline_overlay_large", *canvas);

  // A selected bitmap tip must not change Clone's outline: its strokes stay
  // procedural, so the overlay keeps the circle instead of tracing the bar tip.
  auto& library = window.brush_tip_library();
  const auto tip_id = library.add_tip(QStringLiteral("Bar"), make_bar_tip_image(), 0.25);
  CHECK(!tip_id.isEmpty());
  window.set_active_brush_tip(tip_id, false);
  CHECK(canvas->cursor().shape() == Qt::CrossCursor);
  CHECK(count_dark_ring_pixels() > 20);

  // Shrinking returns to the pixmap cursor, exactly like the Brush.
  canvas->set_brush_size(48);
  CHECK(canvas->cursor().shape() != Qt::CrossCursor);
  clear_brush_tip_test_state();
}

void ui_default_brush_tips_seed_once_and_render_sheet() {
  clear_brush_tip_test_state();
  {
    auto settings = patchy::ui::app_settings();
    settings.setValue(QStringLiteral("brushes/defaultTipsVersion"), 0);
    settings.sync();
  }

  patchy::ui::MainWindow window;
  show_window(window);
  auto& library = window.brush_tip_library();
  const auto specs = patchy::ui::generate_default_brush_tips();
  CHECK(specs.size() == 40);
  CHECK(library.entries().size() == specs.size());
  const auto folder = patchy::ui::default_brush_tips_folder_name();
  for (const auto& entry : library.entries()) {
    CHECK(entry.folder == folder);
    CHECK(entry.size.width() > 0 && entry.size.width() <= 200);
    CHECK(entry.size.height() > 0 && entry.size.height() <= 200);
  }
  CHECK(library.folders() == QStringList{folder});

  // Thumbnails ride on a light paper chip so they stay visible on the dark toolbar/popup;
  // save a strip on the UI background color for visual review.
  {
    constexpr int kThumb = 48;
    constexpr int kPad = 8;
    const auto count = static_cast<int>(library.entries().size());
    QImage strip((kThumb + kPad) * count + kPad, kThumb + 2 * kPad, QImage::Format_RGB32);
    strip.fill(QColor(0x2B, 0x2B, 0x2B));
    QPainter strip_painter(&strip);
    int strip_x = kPad;
    int visible_thumbnails = 0;
    for (const auto& entry : library.entries()) {
      strip_painter.drawPixmap(strip_x, kPad, entry.thumbnail);
      const auto image = entry.thumbnail.toImage();
      int bright_pixels = 0;
      for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
          if (qAlpha(image.pixel(x, y)) > 200 && qGray(image.pixel(x, y)) > 150) {
            ++bright_pixels;
          }
        }
      }
      // Every thumbnail must carry a light backing (not just black-on-transparent ink).
      if (bright_pixels > kThumb * kThumb / 10) {
        ++visible_thumbnails;
      }
      strip_x += kThumb + kPad;
    }
    strip_painter.end();
    CHECK(visible_thumbnails == count);
    ensure_artifact_dir();
    CHECK(strip.save(QStringLiteral("test-artifacts/ui_brush_tip_thumbnails_dark.png")));
  }

  // Deleting a seeded tip must stick: the version gate stops re-seeding. "Restore Defaults"
  // is the explicit way back.
  const auto first_id = library.entries().front().id;
  CHECK(library.remove_tip(first_id));
  {
    patchy::ui::MainWindow second_window;
    show_window(second_window);
    auto& second_library = second_window.brush_tip_library();
    CHECK(second_library.entries().size() == specs.size() - 1);
    CHECK(second_library.restore_default_tips() == 1);
    CHECK(second_library.entries().size() == specs.size());
    CHECK(second_library.restore_default_tips() == 0);  // idempotent when nothing is missing
  }

  // Contact sheet: a real engine stroke plus a single stamp per tip, for visual review.
  constexpr int kCellWidth = 340;
  constexpr int kCellHeight = 96;
  constexpr int kColumns = 2;
  const int rows = (static_cast<int>(specs.size()) + kColumns - 1) / kColumns;
  QImage sheet(kColumns * kCellWidth, rows * kCellHeight, QImage::Format_RGB32);
  sheet.fill(Qt::white);
  QPainter painter(&sheet);
  int cell_index = 0;
  for (const auto& spec : specs) {
    const auto cell_x = (cell_index % kColumns) * kCellWidth;
    const auto cell_y = (cell_index / kColumns) * kCellHeight;
    ++cell_index;

    patchy::Document document(kCellWidth, kCellHeight, patchy::PixelFormat::rgba8());
    patchy::PixelBuffer pixels(kCellWidth, kCellHeight, patchy::PixelFormat::rgba8());
    pixels.clear(0);
    const auto layer_id = document.add_pixel_layer("Stroke", std::move(pixels)).id();
    const auto mips = patchy::build_brush_tip_mips(spec.tip);
    const auto brush_size = 44;
    const auto scaled = patchy::make_scaled_brush_tip(mips, brush_size);
    CHECK(!scaled.empty());
    patchy::EditOptions options;
    options.primary = patchy::EditColor{0, 0, 0, 255};
    options.brush_size = brush_size;
    options.brush_tip = &scaled;
    options.brush_tip_spacing = spec.spacing;
    options.brush_dynamics = spec.dynamics;
    options.brush_dynamics.seed = 0xD3FA017U;
    // Matches the app's stroke behavior at full opacity: overlapping dabs accumulate toward
    // solid coverage (CanvasWidget's stroke compositor), so no per-pixel gate is needed here —
    // plain source-over dab compositing produces the same result.
    patchy::BrushTipStrokeState state;
    double previous_x = 120.0;
    double previous_y = kCellHeight / 2.0;
    for (int step = 1; step <= 32; ++step) {
      const auto t = static_cast<double>(step) / 32.0;
      const auto x = 120.0 + t * (kCellWidth - 180.0);
      const auto y = kCellHeight / 2.0 - std::sin(t * 2.0 * 3.14159265358979323846) * (kCellHeight / 2.0 - 26.0);
      (void)patchy::paint_brush_segment(document, layer_id, previous_x, previous_y, x, y, options, false, state);
      previous_x = x;
      previous_y = y;
    }
    const auto* layer = document.find_layer(layer_id);
    CHECK(layer != nullptr);
    int painted = 0;
    QImage stroke(kCellWidth, kCellHeight, QImage::Format_RGBA8888);
    for (int y = 0; y < kCellHeight; ++y) {
      const auto row = layer->pixels().row(y);
      std::memcpy(stroke.scanLine(y), row.data(), static_cast<std::size_t>(kCellWidth) * 4U);
      for (int x = 0; x < kCellWidth; ++x) {
        painted += row.data()[static_cast<std::size_t>(x) * 4U + 3U] > 0U ? 1 : 0;
      }
    }
    // Dynamic scatter can place most of a sparse tip outside this deliberately small preview;
    // static tips retain the stronger historical coverage check.
    CHECK(painted > (spec.dynamics.active() ? 20 : 200));
    painter.drawImage(cell_x, cell_y, stroke);
    painter.setPen(Qt::black);
    painter.drawText(QRect(cell_x + 6, cell_y + 4, 110, 40), Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap,
                     QStringLiteral("%1\n%2%").arg(spec.name).arg(std::lround(spec.spacing * 100.0)));
    painter.setPen(QColor(0, 0, 0, 30));
    painter.drawRect(cell_x, cell_y, kCellWidth - 1, kCellHeight - 1);
  }
  painter.end();
  ensure_artifact_dir();
  CHECK(sheet.save(QStringLiteral("test-artifacts/ui_default_brush_tips_sheet.png")));

  // since_version filtering: a v2 install gains the v3 and v4 additions on upgrade; the
  // parameterless overload (the manager's Restore button) still restores everything missing.
  {
    patchy::ui::BrushTipLibrary versioned(brush_tip_test_storage_dir() + QStringLiteral("/v2-upgrade"));
    CHECK(versioned.restore_default_tips(2) == 24);  // v3 stamps plus v4 effect presets seed
    CHECK(versioned.restore_default_tips(2) == 0);   // idempotent
    CHECK(versioned.restore_default_tips() == 16);   // explicit restore brings back the originals
    CHECK(versioned.entries().size() == specs.size());
  }
  {
    patchy::ui::BrushTipLibrary versioned(brush_tip_test_storage_dir() + QStringLiteral("/v3-upgrade"));
    CHECK(versioned.restore_default_tips(3) == 4);  // only the v4 effect presets seed
  }

  // Upgrade end-to-end: simulate a v2 install that deleted an original default ("Chalk") and
  // never had a v3 tip ("Snowflake"). The version-gated seeding must re-add only the new tip
  // and leave the deletion alone, then advance the stored version.
  {
    patchy::ui::BrushTipLibrary storage(brush_tip_test_storage_dir());
    const auto entry_id = [&storage, &folder](const QString& name) {
      for (const auto& entry : storage.entries()) {
        if (entry.folder == folder && entry.name == name) {
          return entry.id;
        }
      }
      return QString();
    };
    CHECK(storage.remove_tip(entry_id(QStringLiteral("Chalk"))));
    CHECK(storage.remove_tip(entry_id(QStringLiteral("Snowflake"))));
  }
  {
    auto settings = patchy::ui::app_settings();
    settings.setValue(QStringLiteral("brushes/defaultTipsVersion"), 2);
    settings.sync();
  }
  {
    patchy::ui::MainWindow upgraded_window;
    show_window(upgraded_window);
    auto& upgraded = upgraded_window.brush_tip_library();
    bool has_chalk = false;
    bool has_snowflake = false;
    for (const auto& entry : upgraded.entries()) {
      has_chalk = has_chalk || (entry.folder == folder && entry.name == QStringLiteral("Chalk"));
      has_snowflake =
          has_snowflake || (entry.folder == folder && entry.name == QStringLiteral("Snowflake"));
    }
    CHECK(!has_chalk);      // the deliberate deletion is respected across the upgrade
    CHECK(has_snowflake);   // the tip introduced after v2 is seeded
    CHECK(upgraded.entries().size() == specs.size() - 1);
    CHECK(patchy::ui::app_settings().value(QStringLiteral("brushes/defaultTipsVersion")).toInt() == 4);
  }

  // A stale MASK (a tip seeded before a generator artwork fix) also heals via the factory
  // reset: the PNG is rewritten in place under the same id.
  QString stale_star_id;
  {
    patchy::ui::BrushTipLibrary storage(brush_tip_test_storage_dir());
    for (const auto& entry : storage.entries()) {
      if (entry.folder == folder && entry.name == QStringLiteral("Star")) {
        stale_star_id = entry.id;
      }
    }
    CHECK(!stale_star_id.isEmpty());
    QImage stale(8, 8, QImage::Format_Grayscale8);
    stale.fill(255);
    CHECK(stale.save(
        brush_tip_test_storage_dir() + QStringLiteral("/") + stale_star_id + QStringLiteral(".png"),
        "PNG"));
  }
  {
    patchy::ui::BrushTipLibrary storage(brush_tip_test_storage_dir());
    const auto* stale_star = storage.find_entry(stale_star_id);
    CHECK(stale_star != nullptr);
    CHECK(stale_star->size == QSize(8, 8));
    CHECK(storage.reset_default_tips_to_factory() == 1);  // only the doctored Star differs
    const auto* fixed = storage.find_entry(stale_star_id);
    CHECK(fixed != nullptr);
    CHECK(fixed->size.width() > 100 && fixed->size.height() > 100);
    const auto reloaded_tip = storage.tip(stale_star_id);
    CHECK(reloaded_tip != nullptr);
    CHECK(reloaded_tip->width == fixed->size.width());
    CHECK(storage.reset_default_tips_to_factory() == 0);  // idempotent once factory
  }
  clear_brush_tip_test_state();
}

// The active bitmap tip comes back on the next launch (GitHub issue 254); a tip the
// library has since lost falls back to Round.
void ui_brush_tip_restores_on_startup() {
  SettingsValueRestorer saved_tip(QStringLiteral("tools/brushTip"));
  clear_brush_tip_test_state();
  QString tip_id;
  {
    patchy::ui::MainWindow window;
    show_window(window);
    auto& library = window.brush_tip_library();
    tip_id = library.add_tip(QStringLiteral("Session Bar"), make_bar_tip_image(), 0.5);
    CHECK(!tip_id.isEmpty());
    window.set_active_brush_tip(tip_id, false);
    CHECK(require_canvas(window)->has_brush_tip());
    for (int waited = 0; waited < 5000 && !tool_settings_saved_brush_tip(tip_id); waited += 100) {
      process_events_for(100);  // the debounced tool-settings save records the tip
    }
    CHECK(patchy::ui::app_settings().value(QStringLiteral("tools/brushTip")).toString() == tip_id);
  }
  {
    patchy::ui::MainWindow window;
    show_window(window);
    auto* canvas = require_canvas(window);
    CHECK(canvas->has_brush_tip());
    auto* picker = window.findChild<patchy::ui::BrushTipPicker*>(QStringLiteral("brushTipPicker"));
    CHECK(picker != nullptr);
    CHECK(picker->current_tip_id() == tip_id);
  }
  clear_brush_tip_test_state();  // the tip is gone from the library
  {
    patchy::ui::MainWindow window;
    show_window(window);
    auto* canvas = require_canvas(window);
    CHECK(!canvas->has_brush_tip());
    auto* picker = window.findChild<patchy::ui::BrushTipPicker*>(QStringLiteral("brushTipPicker"));
    CHECK(picker != nullptr);
    CHECK(picker->current_tip_id() == patchy::ui::builtin_round_brush_tip_id());
  }
  clear_brush_tip_test_state();
}

}  // namespace

std::vector<patchy::test::TestCase> brush_pattern_palette_tests_part2() {
  return {
      {"ui_brush_tip_soft_stamps_accumulate_without_seams", ui_brush_tip_soft_stamps_accumulate_without_seams},
      {"ui_brush_outline_overlay_tracks_large_brushes", ui_brush_outline_overlay_tracks_large_brushes},
      {"ui_clone_outline_overlay_tracks_large_brushes", ui_clone_outline_overlay_tracks_large_brushes},
      {"ui_brush_tip_cursor_shows_tip_shape", ui_brush_tip_cursor_shows_tip_shape},
      {"ui_brush_tip_picker_popup_offers_define_from_selection",
       ui_brush_tip_picker_popup_offers_define_from_selection},
      {"ui_brush_tip_picker_keeps_options_bar_height", ui_brush_tip_picker_keeps_options_bar_height},
      {"ui_brush_tip_picker_keeps_a_fixed_width", ui_brush_tip_picker_keeps_a_fixed_width},
      {"ui_brush_tip_follows_the_preset_menu", ui_brush_tip_follows_the_preset_menu},
      {"ui_default_brush_tips_carry_curated_dynamics", ui_default_brush_tips_carry_curated_dynamics},
      {"ui_brush_tip_manager_edits_dynamics", ui_brush_tip_manager_edits_dynamics},
      {"ui_brush_tip_picker_popup_resizes_and_persists", ui_brush_tip_picker_popup_resizes_and_persists},
      {"ui_brush_dynamics_round_brush_session", ui_brush_dynamics_round_brush_session},
      {"ui_brush_dynamics_popup_edits_apply_and_persist", ui_brush_dynamics_popup_edits_apply_and_persist},
      {"ui_brush_dynamics_popup_control_edits_persist", ui_brush_dynamics_popup_control_edits_persist},
      {"ui_brush_dynamics_button_toggles_popup_closed", ui_brush_dynamics_button_toggles_popup_closed},
      {"ui_brush_tip_picker_filter_narrows_and_picks", ui_brush_tip_picker_filter_narrows_and_picks},
      {"ui_brush_tips_window_floats_and_follows_the_tool", ui_brush_tips_window_floats_and_follows_the_tool},
      {"ui_brush_settings_window_floats_and_edits_live", ui_brush_settings_window_floats_and_edits_live},
      {"shot_brush_floating_windows", shot_brush_floating_windows},
      {"shot_brush_keep_open_buttons", shot_brush_keep_open_buttons},
      {"ui_brush_dynamics_popup_tabs_switch_and_remember", ui_brush_dynamics_popup_tabs_switch_and_remember},
      {"ui_brush_dynamics_section_switch_keeps_values", ui_brush_dynamics_section_switch_keeps_values},
      {"ui_brush_dynamics_reset_section_and_default_dots",
       ui_brush_dynamics_reset_section_and_default_dots},
      {"ui_brush_dynamics_stroke_scatters_with_seed", ui_brush_dynamics_stroke_scatters_with_seed},
      {"ui_brush_direction_first_dab_follows_stroke", ui_brush_direction_first_dab_follows_stroke},
      {"ui_brush_color_dynamics_reaches_stroke_compositor",
       ui_brush_color_dynamics_reaches_stroke_compositor},
      {"ui_brush_dynamics_abr_import_carries_dynamics", ui_brush_dynamics_abr_import_carries_dynamics},
      {"ui_default_brush_tips_seed_once_and_render_sheet", ui_default_brush_tips_seed_once_and_render_sheet},
      {"ui_brush_tip_restores_on_startup", ui_brush_tip_restores_on_startup},
  };
}
