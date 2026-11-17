// Background Eraser and Magic Eraser (GitHub issue 727; docs/eraser-tools.md): two tools in the
// Eraser button's flyout. The Background Eraser is a brush that erases the pixels matching a
// sampled color; the Magic Eraser erases the area the Fill tool would flood, with one click.
// Each shows its own options, erases in one undo step, follows the Eraser on a locked
// Background, keeps its options across a restart and is reachable from scripts.
#include "core/document.hpp"
#include "core/document_depth.hpp"
#include "core/layer.hpp"
#include "core/pixel_buffer.hpp"
#include "core/pixel_depth.hpp"
#include "scripting_test_support.hpp"
#include "test_harness.hpp"
#include "ui/app_settings.hpp"
#include "ui/canvas_types.hpp"
#include "ui/canvas_widget.hpp"
#include "ui/main_window.hpp"
#include "ui/modifier_names.hpp"
#include "ui/tool_cursors.hpp"
#include "ui_test_access.hpp"
#include "ui_test_support.hpp"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QImage>
#include <QKeySequence>
#include <QMenu>
#include <QSlider>
#include <QSpinBox>
#include <QStatusBar>
#include <QTest>
#include <QTimer>
#include <QToolButton>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <list>
#include <utility>
#include <vector>

namespace {

using namespace patchy::test::ui;
using patchy::test::ui::scripting_support::backlog_contains;
using patchy::test::ui::scripting_support::run_script;
using patchy::ui::CanvasTool;

const QColor kRed(210, 40, 40);
const QColor kGreen(40, 170, 60);
const QColor kYellow(240, 220, 40);

constexpr std::array<const char*, 13> kEraserSettingsKeys{
    "tools/activeTool",           "tools/backgroundEraserSampling", "tools/backgroundEraserLimits",
    "tools/backgroundEraserTolerance", "tools/backgroundEraserProtectForeground", "tools/magicEraserTolerance",
    "tools/magicEraserAntiAlias",
    "tools/magicEraserContiguous", "tools/magicEraserSampleAllLayers", "tools/magicEraserOpacity",
    "tools/eraserSize",           "tools/eraserSoftness",           "tools/eraserOpacity"};

std::list<SettingsValueRestorer> clear_eraser_settings() {
  std::list<SettingsValueRestorer> restorers;
  auto settings = patchy::ui::app_settings();
  for (const char* key : kEraserSettingsKeys) {
    restorers.emplace_back(QString::fromLatin1(key));
    settings.remove(QLatin1String(key));
  }
  settings.sync();
  return restorers;
}

// A 240x160 opaque layer: white, a red block from (30, 40) to (109, 119) and a green block from
// (150, 30) to (209, 129), made active. Returns false when the script failed.
bool add_test_picture(patchy::ui::MainWindow& window) {
  return run_script(window, QStringLiteral(R"JS(
    var doc = app.activeDocument;
    var layer = doc.addLayer('Picture');
    layer.fillRect(0, 0, 240, 160, '#ffffff');
    layer.fillRect(30, 40, 80, 80, '#d22828');
    layer.fillRect(150, 30, 60, 100, '#28aa3c');
    doc.activeLayer = layer;
  )JS"));
}

// The same picture with a second layer above it, "Top", holding a closed blue frame from
// (0, 122) to (119, 159) that is 10 px thick. The picture stays the active layer.
bool add_fenced_test_picture(patchy::ui::MainWindow& window) {
  return run_script(window, QStringLiteral(R"JS(
    var doc = app.activeDocument;
    var picture = doc.addLayer('Picture');
    picture.fillRect(0, 0, 240, 160, '#ffffff');
    picture.fillRect(30, 40, 80, 80, '#d22828');
    picture.fillRect(150, 30, 60, 100, '#28aa3c');
    var top = doc.addLayer('Top');
    var W = 120, H = 38, data = new Uint8Array(W * H * 4);
    for (var y = 0; y < H; y++) {
      for (var x = 0; x < W; x++) {
        if (x < 10 || x >= W - 10 || y < 10 || y >= H - 10) {
          var i = (y * W + x) * 4;
          data[i] = 30; data[i + 1] = 60; data[i + 2] = 220; data[i + 3] = 255;
        }
      }
    }
    top.setPixels({x: 0, y: 122, width: W, height: H, data: data.buffer});
    doc.activeLayer = picture;
  )JS"));
}

void stroke(patchy::ui::CanvasWidget& canvas, QPoint from, QPoint to) {
  drag_document_pixel_centers(canvas, {from, to}, 12);
  QApplication::processEvents();
}

void click(patchy::ui::CanvasWidget& canvas, QPoint point) {
  drag_document_pixel_centers(canvas, {point, point}, 1);
  QApplication::processEvents();
}

// The active layer's own pixel at a document point: {r, g, b, a} (a = 255 without alpha), or
// all -1 outside its buffer.
std::array<int, 4> layer_pixel(const patchy::Document& document, QPoint point) {
  const auto* layer = document.active_layer_id().has_value() ? document.find_layer(*document.active_layer_id()) : nullptr;
  if (layer == nullptr) {
    return {-1, -1, -1, -1};
  }
  const auto bounds = layer->bounds();
  const auto& pixels = layer->pixels();
  const auto x = point.x() - bounds.x;
  const auto y = point.y() - bounds.y;
  if (x < 0 || y < 0 || x >= pixels.width() || y >= pixels.height()) {
    return {-1, -1, -1, -1};
  }
  const auto rgba = patchy::display_rgba8_at(pixels, x, y);
  return {rgba[0], rgba[1], rgba[2], rgba[3]};
}

int layer_alpha(const patchy::Document& document, QPoint point) {
  return layer_pixel(document, point)[3];
}

bool is_color(const std::array<int, 4>& pixel, QColor color, int tolerance = 2) {
  return std::abs(pixel[0] - color.red()) <= tolerance && std::abs(pixel[1] - color.green()) <= tolerance &&
         std::abs(pixel[2] - color.blue()) <= tolerance;
}

void ui_eraser_flyout_holds_the_three_erasers_and_shift_e_cycles() {
  const auto restorers = clear_eraser_settings();
  auto&& [window, canvas] = ShownWindowFixture{};
  auto* button = window.findChild<QToolButton*>(QStringLiteral("eraserToolButton"));
  CHECK(button != nullptr && button->menu() != nullptr);
  if (button == nullptr || button->menu() == nullptr) {
    return;
  }
  CHECK(button->menu()->objectName() == QStringLiteral("eraserToolMenu"));
  CHECK(button->property("toolFlyout").toBool());
  const std::pair<const char*, CanvasTool> order[] = {
      {"toolEraserAction", CanvasTool::Eraser},
      {"toolBackgroundEraserAction", CanvasTool::BackgroundEraser},
      {"toolMagicEraserAction", CanvasTool::MagicEraser},
  };
  const auto entries = button->menu()->actions();
  CHECK(entries.size() == 5);  // three tools, separator, Cycle Eraser Tools
  for (std::size_t i = 0; i < std::size(order); ++i) {
    CHECK(entries.value(static_cast<int>(i)) == require_action(window, order[i].first));
  }
  CHECK(entries.value(3) != nullptr && entries.value(3)->isSeparator());
  auto* cycle = require_action(window, "toolCycleEraserAction");
  CHECK(entries.value(4) == cycle);
  CHECK(button->defaultAction() == require_action(window, "toolEraserAction"));
  CHECK(require_action(window, "toolEraserAction")->text() == QStringLiteral("Eraser"));
  CHECK(require_action(window, "toolBackgroundEraserAction")->text() == QStringLiteral("Background Eraser"));
  CHECK(require_action(window, "toolMagicEraserAction")->text() == QStringLiteral("Magic Eraser"));

  // E is the Eraser's key, the other two ship without one, and Shift+E walks the group.
  CHECK(require_action(window, "toolEraserAction")->shortcut() == QKeySequence(Qt::Key_E));
  CHECK(require_action(window, "toolBackgroundEraserAction")->shortcut().isEmpty());
  CHECK(require_action(window, "toolMagicEraserAction")->shortcut().isEmpty());
  CHECK(cycle->shortcut() == QKeySequence(Qt::SHIFT | Qt::Key_E));
  const auto& registry = window.hotkey_registry();
  for (const char* id : {"tools.eraser", "tools.background_eraser", "tools.magic_eraser", "tools.cycle.eraser"}) {
    const auto* command = registry.find_command(QString::fromLatin1(id));
    CHECK(command != nullptr);
    CHECK(command != nullptr && command->category == QStringLiteral("tools"));
  }
  CHECK(registry.find_command(QStringLiteral("tools.cycle.eraser"))->action == cycle);

  require_action(window, "toolBrushAction")->trigger();
  QApplication::processEvents();
  QTest::keyClick(canvas, Qt::Key_E);
  QApplication::processEvents();
  CHECK(canvas->tool() == CanvasTool::Eraser);
  for (std::size_t step = 1; step <= std::size(order); ++step) {
    QTest::keyClick(canvas, Qt::Key_E, Qt::ShiftModifier);
    QApplication::processEvents();
    const auto& expected = order[step % std::size(order)];
    CHECK(canvas->tool() == expected.second);
    CHECK(button->defaultAction() == require_action(window, expected.first));
  }
  // The plain letter comes back to the eraser the button shows.
  require_action(window, "toolMagicEraserAction")->trigger();
  require_action(window, "toolBrushAction")->trigger();
  QApplication::processEvents();
  QTest::keyClick(canvas, Qt::Key_E);
  QApplication::processEvents();
  CHECK(canvas->tool() == CanvasTool::MagicEraser);

  // The issue's AFTER shot: the open flyout.
  require_action(window, "toolEraserAction")->trigger();
  QApplication::processEvents();
  auto* menu = button->menu();
  bool shown = false;
  QTimer::singleShot(0, [&] {
    shown = menu->isVisible();
    save_window_popup_artifact("issue_727_eraser_flyout", window, *button, *menu);
    menu->close();
  });
  button->showMenu();
  QApplication::processEvents();
  CHECK(shown);
}

void ui_background_eraser_erases_the_sampled_color_in_one_undo_step() {
  const auto restorers = clear_eraser_settings();
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_zoom(1.0);
  CHECK(add_test_picture(window));
  QApplication::processEvents();
  auto* document = patchy::ui::MainWindowTestAccess::document_for_canvas(window, canvas);
  CHECK(document != nullptr);
  if (document == nullptr) {
    return;
  }

  require_action(window, "toolBackgroundEraserAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->tool() == CanvasTool::BackgroundEraser);

  // Its options bar: the tip, Size and Soft like the Eraser, then its own controls with their defaults.
  auto* sampling = window.findChild<QComboBox*>(QStringLiteral("backgroundEraserSamplingCombo"));
  auto* limits = window.findChild<QComboBox*>(QStringLiteral("backgroundEraserLimitsCombo"));
  auto* tolerance = window.findChild<QSpinBox*>(QStringLiteral("backgroundEraserToleranceSpin"));
  auto* size = window.findChild<QSpinBox*>(QStringLiteral("brushSizeSpin"));
  auto* soft = window.findChild<QSpinBox*>(QStringLiteral("brushSoftnessSpin"));
  auto* opacity = window.findChild<QSpinBox*>(QStringLiteral("brushOpacitySpin"));
  auto* replace_sampling = window.findChild<QComboBox*>(QStringLiteral("colorReplaceSamplingCombo"));
  auto* replace_mode = window.findChild<QComboBox*>(QStringLiteral("colorReplaceModeCombo"));
  auto* magic_tolerance = window.findChild<QSpinBox*>(QStringLiteral("magicEraserToleranceSpin"));
  CHECK(sampling != nullptr && limits != nullptr && tolerance != nullptr && size != nullptr && soft != nullptr &&
        opacity != nullptr && replace_sampling != nullptr && replace_mode != nullptr && magic_tolerance != nullptr);
  if (sampling == nullptr || limits == nullptr || tolerance == nullptr || size == nullptr || soft == nullptr ||
      opacity == nullptr || replace_sampling == nullptr || replace_mode == nullptr || magic_tolerance == nullptr) {
    return;
  }
  for (QWidget* shown : std::initializer_list<QWidget*>{sampling, limits, tolerance, size, soft}) {
    CHECK(shown->isVisible());
  }
  for (QWidget* hidden : std::initializer_list<QWidget*>{opacity, replace_sampling, replace_mode, magic_tolerance}) {
    CHECK(!hidden->isVisible());
  }
  CHECK(sampling->count() == 3 && sampling->currentText() == QStringLiteral("Continuous"));
  CHECK(limits->count() == 3 && limits->currentText() == QStringLiteral("Contiguous"));
  CHECK(tolerance->value() == 30 && tolerance->minimum() == 0 && tolerance->maximum() == 100);

  // One stroke inside the red block: the red under the brush goes transparent; the white the
  // brush also covers, the red it never reached and the green block stay.
  size->setValue(60);
  soft->setValue(0);
  QApplication::processEvents();
  CHECK(canvas->brush_size() == 60);
  CHECK(layer_alpha(*document, QPoint(70, 80)) == 255);
  const auto depth_before = patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);
  stroke(*canvas, QPoint(45, 80), QPoint(95, 80));
  CHECK(layer_alpha(*document, QPoint(70, 80)) == 0);
  CHECK(layer_alpha(*document, QPoint(50, 70)) == 0);
  CHECK(layer_alpha(*document, QPoint(104, 90)) == 0);
  CHECK(is_color(layer_pixel(*document, QPoint(70, 80)), kRed));         // the color stays, only alpha falls
  CHECK(layer_alpha(*document, QPoint(70, 44)) == 255);                  // red the brush never reached
  CHECK(layer_alpha(*document, QPoint(118, 80)) == 255);                 // white under the brush
  CHECK(layer_alpha(*document, QPoint(170, 80)) == 255);
  save_widget_artifact("ui_background_eraser_after", window);

  // The whole stroke is one undo step.
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == depth_before + 1);
  patchy::ui::MainWindowTestAccess::undo(window);
  QApplication::processEvents();
  CHECK(layer_alpha(*document, QPoint(70, 80)) == 255);
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == depth_before);

  // Background Swatch with Discontiguous: a stroke that starts on red erases only the
  // background color (green here), wherever the brush reaches.
  canvas->set_secondary_color(kGreen);
  sampling->setCurrentIndex(sampling->findText(QStringLiteral("Background Swatch")));
  limits->setCurrentIndex(limits->findText(QStringLiteral("Discontiguous")));
  QApplication::processEvents();
  CHECK(canvas->background_eraser_sampling() == patchy::ColorMatchSampling::BackgroundSwatch);
  CHECK(canvas->background_eraser_options().limits == patchy::ColorMatchLimits::Discontiguous);
  stroke(*canvas, QPoint(100, 80), QPoint(180, 80));
  CHECK(layer_alpha(*document, QPoint(170, 80)) == 0);
  CHECK(layer_alpha(*document, QPoint(100, 80)) == 255);  // red under the brush, not the swatch color
  CHECK(layer_alpha(*document, QPoint(125, 80)) == 255);

  // A selection limits the erase: only the part of the green inside it goes.
  patchy::ui::MainWindowTestAccess::undo(window);
  QApplication::processEvents();
  CHECK(layer_alpha(*document, QPoint(170, 80)) == 255);
  canvas->set_tool(CanvasTool::Marquee);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(0, 0)),
       canvas->widget_position_for_document_point(QPoint(240, 80)));
  QApplication::processEvents();
  CHECK(canvas->has_selection());
  require_action(window, "toolBackgroundEraserAction")->trigger();
  QApplication::processEvents();
  stroke(*canvas, QPoint(100, 80), QPoint(180, 80));
  CHECK(layer_alpha(*document, QPoint(170, 70)) == 0);
  CHECK(layer_alpha(*document, QPoint(170, 90)) == 255);
  canvas->clear_selection();

  // The Eraser is one click away in the same flyout, with its own bar (Opacity, no Sampling).
  require_action(window, "toolEraserAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->tool() == CanvasTool::Eraser);
  CHECK(opacity->isVisible() && !sampling->isVisible() && !tolerance->isVisible());
  // The two brushes share the Eraser's Size.
  CHECK(canvas->brush_size() == 60);
}

void ui_magic_eraser_click_erases_the_flood_region() {
  const auto restorers = clear_eraser_settings();
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_zoom(1.0);
  CHECK(add_fenced_test_picture(window));
  QApplication::processEvents();
  auto* document = patchy::ui::MainWindowTestAccess::document_for_canvas(window, canvas);
  CHECK(document != nullptr);
  if (document == nullptr) {
    return;
  }
  // The picture is the active layer, under the frame layer.
  const auto* active = document->active_layer_id().has_value()
                           ? std::as_const(*document).find_layer(*document->active_layer_id())
                           : nullptr;
  CHECK(active != nullptr && active->name() == "Picture");

  require_action(window, "toolMagicEraserAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->tool() == CanvasTool::MagicEraser);
  auto* tolerance = window.findChild<QSpinBox*>(QStringLiteral("magicEraserToleranceSpin"));
  auto* anti_alias = window.findChild<QCheckBox*>(QStringLiteral("magicEraserAntiAliasCheck"));
  auto* contiguous = window.findChild<QCheckBox*>(QStringLiteral("magicEraserContiguousCheck"));
  auto* sample_all = window.findChild<QCheckBox*>(QStringLiteral("magicEraserSampleAllLayersCheck"));
  auto* opacity = window.findChild<QSpinBox*>(QStringLiteral("magicEraserOpacitySpin"));
  auto* opacity_slider = window.findChild<QSlider*>(QStringLiteral("magicEraserOpacitySlider"));
  auto* brush_size = window.findChild<QSpinBox*>(QStringLiteral("brushSizeSpin"));
  auto* fill_tolerance = window.findChild<QSpinBox*>(QStringLiteral("fillToleranceSpin"));
  CHECK(tolerance != nullptr && anti_alias != nullptr && contiguous != nullptr && sample_all != nullptr &&
        opacity != nullptr && opacity_slider != nullptr && brush_size != nullptr && fill_tolerance != nullptr);
  if (tolerance == nullptr || anti_alias == nullptr || contiguous == nullptr || sample_all == nullptr ||
      opacity == nullptr || opacity_slider == nullptr || brush_size == nullptr || fill_tolerance == nullptr) {
    return;
  }
  for (QWidget* shown : std::initializer_list<QWidget*>{tolerance, anti_alias, contiguous, sample_all, opacity}) {
    CHECK(shown->isVisible());
  }
  CHECK(!brush_size->isVisible() && !fill_tolerance->isVisible());
  CHECK(tolerance->value() == 32 && tolerance->minimum() == 0 && tolerance->maximum() == 255);
  CHECK(anti_alias->isChecked() && contiguous->isChecked() && !sample_all->isChecked());
  CHECK(opacity->value() == 100 && opacity_slider->value() == 100);

  // One click on the red block: the whole block goes, nothing else, in one undo step.
  const auto depth_before = patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);
  click(*canvas, QPoint(70, 80));
  CHECK(layer_alpha(*document, QPoint(70, 80)) == 0);
  CHECK(layer_alpha(*document, QPoint(31, 41)) == 0);
  CHECK(layer_alpha(*document, QPoint(108, 118)) == 0);
  CHECK(is_color(layer_pixel(*document, QPoint(70, 80)), kRed));
  CHECK(layer_alpha(*document, QPoint(20, 80)) == 255);
  CHECK(layer_alpha(*document, QPoint(170, 80)) == 255);
  save_widget_artifact("ui_magic_eraser_after", window);
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == depth_before + 1);
  patchy::ui::MainWindowTestAccess::undo(window);
  QApplication::processEvents();
  CHECK(layer_alpha(*document, QPoint(70, 80)) == 255);
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == depth_before);

  // Opacity 50% erases half.
  opacity->setValue(50);
  QApplication::processEvents();
  CHECK(opacity_slider->value() == 50 && canvas->magic_eraser_options().opacity == 50);
  click(*canvas, QPoint(70, 80));
  CHECK(std::abs(layer_alpha(*document, QPoint(70, 80)) - 127) <= 2);
  patchy::ui::MainWindowTestAccess::undo(window);
  opacity->setValue(100);
  QApplication::processEvents();

  // Contiguous: the white around the blocks is one area; the white enclosed nowhere else.
  // With Contiguous off, a second red block far from the click goes too.
  CHECK(run_script(window, QStringLiteral("app.activeDocument.activeLayer.fillRect(120, 135, 20, 15, '#d22828');")));
  QApplication::processEvents();
  click(*canvas, QPoint(70, 80));
  CHECK(layer_alpha(*document, QPoint(70, 80)) == 0);
  CHECK(layer_alpha(*document, QPoint(130, 140)) == 255);
  patchy::ui::MainWindowTestAccess::undo(window);
  contiguous->setChecked(false);
  QApplication::processEvents();
  CHECK(!canvas->magic_eraser_options().contiguous);
  click(*canvas, QPoint(70, 80));
  CHECK(layer_alpha(*document, QPoint(70, 80)) == 0);
  CHECK(layer_alpha(*document, QPoint(130, 140)) == 0);
  CHECK(layer_alpha(*document, QPoint(170, 80)) == 255);
  patchy::ui::MainWindowTestAccess::undo(window);
  contiguous->setChecked(true);
  QApplication::processEvents();

  // Sample All Layers: the blue frame on the layer above fences off part of what the click
  // sees. The picture's white inside and under the frame stays; without the option the
  // layer's own white is one area and goes.
  CHECK(canvas->magic_eraser_options().contiguous);
  sample_all->setChecked(true);
  QApplication::processEvents();
  CHECK(canvas->magic_eraser_options().sample_all_layers);
  click(*canvas, QPoint(125, 10));
  CHECK(layer_alpha(*document, QPoint(125, 10)) == 0);
  CHECK(layer_alpha(*document, QPoint(60, 140)) == 255);  // white fenced in by the blue above it
  CHECK(layer_alpha(*document, QPoint(60, 126)) == 255);  // white under the blue itself
  CHECK(layer_alpha(*document, QPoint(70, 80)) == 255);
  patchy::ui::MainWindowTestAccess::undo(window);
  sample_all->setChecked(false);
  QApplication::processEvents();
  click(*canvas, QPoint(125, 10));
  CHECK(layer_alpha(*document, QPoint(125, 10)) == 0);
  CHECK(layer_alpha(*document, QPoint(60, 140)) == 0);
  CHECK(layer_alpha(*document, QPoint(70, 80)) == 255);
}

// The Eraser's rule holds for both tools (docs/tools.md): under a transparency lock (the locked
// Background) the erased pixels take the background color and keep their alpha; an unlocked
// layer without alpha gains one; Lock Image Pixels refuses the edit.
void ui_eraser_tools_follow_the_eraser_on_a_locked_background() {
  constexpr int kWidth = 200;
  constexpr int kHeight = 120;
  const auto make_document = [&](patchy::LayerLockFlags lock_flags) {
    auto pixels = solid_pixels(kWidth, kHeight, patchy::PixelFormat::rgb8(), kRed);
    fill_pixel_rect(pixels, QRect(120, 0, 80, kHeight), kGreen);
    patchy::Document document(kWidth, kHeight, patchy::PixelFormat::rgb8());
    auto& layer = document.add_pixel_layer("Background", std::move(pixels));
    layer.set_lock_flags(lock_flags);
    return document;
  };
  const auto run = [&](patchy::Document& document, CanvasTool tool) {
    patchy::ui::CanvasWidget canvas;
    canvas.resize(420, 320);
    canvas.set_document(&document);
    canvas.set_zoom(1.0);
    canvas.show();
    QApplication::processEvents();
    canvas.set_tool(tool);
    canvas.set_secondary_color(kYellow);
    canvas.set_brush_size(40);
    canvas.set_brush_softness(0);
    if (tool == CanvasTool::BackgroundEraser) {
      stroke(canvas, QPoint(30, 60), QPoint(100, 60));
    } else {
      click(canvas, QPoint(60, 60));
    }
  };
  for (const auto tool : {CanvasTool::BackgroundEraser, CanvasTool::MagicEraser}) {
    // Locked transparency: the background color, no alpha channel gained.
    auto locked = make_document(patchy::kLayerLockTransparentPixels | patchy::kLayerLockPosition);
    run(locked, tool);
    CHECK(std::as_const(locked).layers().front().pixels().format().channels == 3);
    CHECK(is_color(layer_pixel(locked, QPoint(60, 60)), kYellow));
    CHECK(is_color(layer_pixel(locked, QPoint(160, 60)), kGreen));
    if (tool == CanvasTool::BackgroundEraser) {
      CHECK(is_color(layer_pixel(locked, QPoint(60, 110)), kRed));  // outside the brush
    } else {
      CHECK(is_color(layer_pixel(locked, QPoint(60, 110)), kYellow));  // the whole red area
    }

    // Unlocked: the layer gains alpha and the red goes transparent.
    auto unlocked = make_document(patchy::kLayerLockNone);
    run(unlocked, tool);
    CHECK(std::as_const(unlocked).layers().front().pixels().format().channels == 4);
    CHECK(layer_alpha(unlocked, QPoint(60, 60)) == 0);
    CHECK(is_color(layer_pixel(unlocked, QPoint(60, 60)), kRed));
    CHECK(layer_alpha(unlocked, QPoint(160, 60)) == 255);

    // Lock Image Pixels refuses it.
    auto refused = make_document(patchy::kLayerLockImagePixels);
    run(refused, tool);
    CHECK(std::as_const(refused).layers().front().pixels().format().channels == 3);
    CHECK(is_color(layer_pixel(refused, QPoint(60, 60)), kRed));
  }
}

void ui_eraser_tool_options_survive_a_restart() {
  const auto restorers = clear_eraser_settings();
  {
    auto&& [window, canvas] = ShownWindowFixture{};
    require_action(window, "toolBackgroundEraserAction")->trigger();
    QApplication::processEvents();
    auto* sampling = window.findChild<QComboBox*>(QStringLiteral("backgroundEraserSamplingCombo"));
    auto* limits = window.findChild<QComboBox*>(QStringLiteral("backgroundEraserLimitsCombo"));
    auto* tolerance = window.findChild<QSpinBox*>(QStringLiteral("backgroundEraserToleranceSpin"));
    auto* magic_tolerance = window.findChild<QSpinBox*>(QStringLiteral("magicEraserToleranceSpin"));
    auto* anti_alias = window.findChild<QCheckBox*>(QStringLiteral("magicEraserAntiAliasCheck"));
    auto* contiguous = window.findChild<QCheckBox*>(QStringLiteral("magicEraserContiguousCheck"));
    auto* sample_all = window.findChild<QCheckBox*>(QStringLiteral("magicEraserSampleAllLayersCheck"));
    auto* opacity = window.findChild<QSpinBox*>(QStringLiteral("magicEraserOpacitySpin"));
    CHECK(sampling != nullptr && limits != nullptr && tolerance != nullptr && magic_tolerance != nullptr &&
          anti_alias != nullptr && contiguous != nullptr && sample_all != nullptr && opacity != nullptr);
    if (sampling == nullptr || limits == nullptr || tolerance == nullptr || magic_tolerance == nullptr ||
        anti_alias == nullptr || contiguous == nullptr || sample_all == nullptr || opacity == nullptr) {
      return;
    }
    sampling->setCurrentIndex(sampling->findText(QStringLiteral("Once")));
    limits->setCurrentIndex(limits->findText(QStringLiteral("Find Edges")));
    tolerance->setValue(55);
    magic_tolerance->setValue(77);
    anti_alias->setChecked(false);
    contiguous->setChecked(false);
    sample_all->setChecked(true);
    opacity->setValue(40);
    QApplication::processEvents();
    // The controls reach the canvas at once.
    CHECK(canvas->background_eraser_sampling() == patchy::ColorMatchSampling::Once);
    CHECK(canvas->background_eraser_options().limits == patchy::ColorMatchLimits::FindEdges);
    CHECK(canvas->background_eraser_options().tolerance == 55);
    const patchy::ui::MagicEraserOptions expected{77, false, false, true, 40};
    CHECK(canvas->magic_eraser_options() == expected);
    patchy::ui::MainWindowTestAccess::save_tool_settings(window);
    auto settings = patchy::ui::app_settings();
    CHECK(settings.value(QStringLiteral("tools/activeTool")).toString() == QStringLiteral("tools.background_eraser"));
    CHECK(settings.value(QStringLiteral("tools/backgroundEraserSampling")).toString() == QStringLiteral("once"));
    CHECK(settings.value(QStringLiteral("tools/backgroundEraserLimits")).toString() == QStringLiteral("findEdges"));
    CHECK(settings.value(QStringLiteral("tools/backgroundEraserTolerance")).toInt() == 55);
    CHECK(settings.value(QStringLiteral("tools/magicEraserTolerance")).toInt() == 77);
    CHECK(!settings.value(QStringLiteral("tools/magicEraserAntiAlias")).toBool());
    CHECK(!settings.value(QStringLiteral("tools/magicEraserContiguous")).toBool());
    CHECK(settings.value(QStringLiteral("tools/magicEraserSampleAllLayers")).toBool());
    CHECK(settings.value(QStringLiteral("tools/magicEraserOpacity")).toInt() == 40);
  }
  {
    // "The next launch": a fresh window from the same settings store.
    auto&& [window, canvas] = ShownWindowFixture{};
    CHECK(canvas->tool() == CanvasTool::BackgroundEraser);
    auto* button = window.findChild<QToolButton*>(QStringLiteral("eraserToolButton"));
    CHECK(button != nullptr && button->defaultAction() == require_action(window, "toolBackgroundEraserAction"));
    CHECK(canvas->background_eraser_sampling() == patchy::ColorMatchSampling::Once);
    CHECK(canvas->background_eraser_options().limits == patchy::ColorMatchLimits::FindEdges);
    CHECK(canvas->background_eraser_options().tolerance == 55);
    const patchy::ui::MagicEraserOptions expected{77, false, false, true, 40};
    CHECK(canvas->magic_eraser_options() == expected);
    auto* sampling = window.findChild<QComboBox*>(QStringLiteral("backgroundEraserSamplingCombo"));
    auto* limits = window.findChild<QComboBox*>(QStringLiteral("backgroundEraserLimitsCombo"));
    auto* tolerance = window.findChild<QSpinBox*>(QStringLiteral("backgroundEraserToleranceSpin"));
    auto* magic_tolerance = window.findChild<QSpinBox*>(QStringLiteral("magicEraserToleranceSpin"));
    auto* sample_all = window.findChild<QCheckBox*>(QStringLiteral("magicEraserSampleAllLayersCheck"));
    auto* opacity = window.findChild<QSpinBox*>(QStringLiteral("magicEraserOpacitySpin"));
    CHECK(sampling != nullptr && sampling->currentText() == QStringLiteral("Once"));
    CHECK(limits != nullptr && limits->currentText() == QStringLiteral("Find Edges"));
    CHECK(tolerance != nullptr && tolerance->value() == 55);
    CHECK(magic_tolerance != nullptr && magic_tolerance->value() == 77);
    CHECK(sample_all != nullptr && sample_all->isChecked());
    CHECK(opacity != nullptr && opacity->value() == 40);
  }
  {
    // Unknown tokens and unreadable numbers fall back to the defaults.
    auto settings = patchy::ui::app_settings();
    settings.setValue(QStringLiteral("tools/backgroundEraserSampling"), QStringLiteral("twice"));
    settings.setValue(QStringLiteral("tools/backgroundEraserLimits"), 7);
    settings.setValue(QStringLiteral("tools/backgroundEraserTolerance"), QStringLiteral("lots"));
    settings.setValue(QStringLiteral("tools/magicEraserTolerance"), QStringLiteral("lots"));
    settings.setValue(QStringLiteral("tools/magicEraserAntiAlias"), QStringLiteral("maybe"));
    settings.setValue(QStringLiteral("tools/magicEraserContiguous"), QStringLiteral("maybe"));
    settings.setValue(QStringLiteral("tools/magicEraserSampleAllLayers"), QStringLiteral("maybe"));
    settings.setValue(QStringLiteral("tools/magicEraserOpacity"), QStringLiteral("lots"));
    settings.sync();
    auto&& [window, canvas] = ShownWindowFixture{};
    CHECK(canvas->background_eraser_options() == patchy::BackgroundEraserOptions{});
    CHECK(canvas->background_eraser_sampling() == patchy::ColorMatchSampling::Continuous);
    CHECK(canvas->magic_eraser_options() == patchy::ui::MagicEraserOptions{});
  }
}

// A 16-bit layer is erased at its own depth: its colors, values between 8-bit steps, survive
// the erase unrounded.
void ui_eraser_tools_erase_16_bit_layers_at_depth() {
  constexpr int kWidth = 200;
  constexpr int kHeight = 120;
  for (const auto tool : {CanvasTool::BackgroundEraser, CanvasTool::MagicEraser}) {
    auto pixels = solid_pixels(kWidth, kHeight, patchy::PixelFormat::rgba8(), kRed);
    fill_pixel_rect(pixels, QRect(120, 0, 80, kHeight), kGreen);
    patchy::Document document(kWidth, kHeight, patchy::PixelFormat::rgba8());
    const auto layer_id = document.add_pixel_layer("Photo", std::move(pixels)).id();
    patchy::convert_document_depth(document, patchy::BitDepth::UInt16);
    {
      // Nudge every sample off the 8-bit grid so byte rounding would show.
      auto& deep = document.find_layer(layer_id)->pixels();
      for (int y = 0; y < deep.height(); ++y) {
        for (int x = 0; x < deep.width(); ++x) {
          auto values = patchy::load_pixel(deep.format(), deep.pixel(x, y));
          values[0] += 0.25F;
          patchy::store_pixel(deep.format(), deep.pixel(x, y), values);
        }
      }
    }
    patchy::ui::CanvasWidget canvas;
    canvas.resize(420, 320);
    canvas.set_document(&document);
    canvas.set_zoom(1.0);
    canvas.show();
    QApplication::processEvents();
    canvas.set_tool(tool);
    canvas.set_brush_size(40);
    canvas.set_brush_softness(0);
    auto magic = canvas.magic_eraser_options();
    magic.opacity = 50;
    canvas.set_magic_eraser_options(magic);
    const auto value_at = [&](int x, int y) {
      const auto& deep = std::as_const(*document.find_layer(layer_id)).pixels();
      return patchy::load_pixel(deep.format(), deep.pixel(x, y));
    };
    const auto red_before = value_at(60, 60);
    const auto green_before = value_at(130, 60);
    if (tool == CanvasTool::BackgroundEraser) {
      stroke(canvas, QPoint(30, 60), QPoint(110, 60));
    } else {
      click(canvas, QPoint(60, 60));
    }
    CHECK(std::as_const(*document.find_layer(layer_id)).pixels().format().bit_depth == patchy::BitDepth::UInt16);
    const auto erased = value_at(60, 60);
    for (std::size_t channel = 0; channel < 3; ++channel) {
      CHECK(erased[channel] == red_before[channel]);  // the off-grid color survives
    }
    if (tool == CanvasTool::BackgroundEraser) {
      CHECK(erased[3] == 0.0F);
    } else {
      CHECK(std::abs(erased[3] - 127.0F) < 1.0F);  // Opacity 50%
    }
    CHECK(value_at(130, 60) == green_before);  // the green is not the sampled red
  }
}

void ui_script_erase_background_and_magic_erase() {
  const auto restorers = clear_eraser_settings();
  patchy::ui::MainWindow window;
  show_window(window);
  CHECK(add_test_picture(window));
  CHECK(run_script(window, QStringLiteral(R"JS(
    var doc = app.activeDocument;
    var layer = doc.activeLayer;
    var at = function (x, y) {
      var image = layer.getPixels();
      var px = new Uint8Array(image.data);
      var i = ((y - image.y) * image.width + (x - image.x)) * 4;
      return px[i] + ',' + px[i + 1] + ',' + px[i + 2] + ',' + px[i + 3];
    };
    // Background Eraser: one stroke inside the red block.
    var r = layer.eraseBackground({points: [{x: 45, y: 80}, {x: 95, y: 80}], size: 60});
    console.log('stroke=' + (r.width > 0 && r.height > 0) + ' inside=' + (r.x >= 30 && r.x + r.width <= 111));
    console.log('red=' + at(70, 80) + ' white=' + at(118, 80) + ' far=' + at(70, 44));
    // The background color through a brush that never centers on it.
    var g = layer.eraseBackground({points: [{x: 130, y: 80}, {x: 140, y: 80}], size: 80, sampling: 'background',
                                   backgroundColor: '#28aa3c', limits: 'discontiguous', tolerance: 10,
                                   hardness: 100});
    console.log('swatch=' + at(160, 80) + ' box=' + (g.x >= 150));
    // Nothing matches: no error, an empty box.
    var none = layer.eraseBackground({points: [{x: 125, y: 20}], size: 10, sampling: 'background',
                                      backgroundColor: '#000000'});
    console.log('none=' + none.width + 'x' + none.height);
    // protectColor: a stroke from the white keeps the near-white it names and still erases white.
    layer.fillRect(0, 130, 30, 30, '#e6e6e6');
    var k = layer.eraseBackground({points: [{x: 60, y: 145}, {x: 20, y: 145}], size: 20, sampling: 'once',
                                   limits: 'discontiguous', protectColor: '#e6e6e6'});
    console.log('protect=' + at(50, 145) + ' kept=' + at(15, 145) + ' box=' + (k.x >= 30));
    var u = layer.eraseBackground({points: [{x: 60, y: 145}, {x: 20, y: 145}], size: 20, sampling: 'once',
                                   limits: 'discontiguous'});
    console.log('unprotected=' + at(15, 145));
    // Magic Eraser: the rest of the green block with one click, then half of the white.
    var m = layer.magicErase({x: 200, y: 35});
    console.log('magic=' + at(205, 125) + ' box=' + m.x + ',' + m.y + ',' + m.width + ',' + m.height);
    var h = layer.magicErase({x: 5, y: 5, opacity: 50, tolerance: 0, antiAlias: false, contiguous: true,
                              sampleAllLayers: false});
    var w = Number(at(5, 5).split(',')[3]);
    console.log('half=' + (w > 120 && w < 135) + ' wide=' + (h.width === 240 && h.height === 160));
    var miss = layer.magicErase({x: 70, y: 80});
    console.log('miss=' + miss.width + 'x' + miss.height);
    var refuse = function (name, call) {
      try {
        call();
        console.log(name + '-no-throw');
      } catch (e) {
        console.log(name + '-refused=' + (e.message.length > 0));
      }
    };
    refuse('nopoints', function () { layer.eraseBackground({size: 10}); });
    refuse('noobject', function () { layer.eraseBackground(); });
    refuse('unknown', function () { layer.eraseBackground({points: [{x: 1, y: 1}], color: '#1e3cdc'}); });
    refuse('limits', function () { layer.eraseBackground({points: [{x: 1, y: 1}], limits: 'findedges'}); });
    refuse('sampling', function () { layer.eraseBackground({points: [{x: 1, y: 1}], sampling: 3}); });
    refuse('tolerance', function () { layer.eraseBackground({points: [{x: 1, y: 1}], tolerance: 101}); });
    refuse('swatch', function () { layer.eraseBackground({points: [{x: 1, y: 1}], sampling: 'background'}); });
    refuse('protect', function () { layer.eraseBackground({points: [{x: 1, y: 1}], protectColor: 7}); });
    refuse('point', function () { layer.eraseBackground({points: [{x: 1}]}); });
    refuse('nox', function () { layer.magicErase({y: 3}); });
    refuse('magicunknown', function () { layer.magicErase({x: 1, y: 1, size: 3}); });
    refuse('magictolerance', function () { layer.magicErase({x: 1, y: 1, tolerance: 256}); });
    refuse('magicbool', function () { layer.magicErase({x: 1, y: 1, contiguous: 1}); });
    refuse('magicopacity', function () { layer.magicErase({x: 1, y: 1, opacity: -1}); });
    var other = doc.addLayer('Other');
    doc.activeLayer = other;
    refuse('inactive', function () { layer.magicErase({x: 1, y: 1}); });
    refuse('inactivebrush', function () { layer.eraseBackground({points: [{x: 1, y: 1}]}); });
  )JS")));
  CHECK(backlog_contains(window, QStringLiteral("stroke=true inside=true")));
  CHECK(backlog_contains(window, QStringLiteral("red=210,40,40,0 white=255,255,255,255 far=210,40,40,255")));
  CHECK(backlog_contains(window, QStringLiteral("swatch=40,170,60,0 box=true")));
  CHECK(backlog_contains(window, QStringLiteral("none=0x0")));
  CHECK(backlog_contains(window, QStringLiteral("protect=255,255,255,0 kept=230,230,230,255 box=true")));
  CHECK(backlog_contains(window, QStringLiteral("unprotected=230,230,230,0")));
  CHECK(backlog_contains(window, QStringLiteral("magic=40,170,60,0 box=150,30,60,100")));
  CHECK(backlog_contains(window, QStringLiteral("half=true wide=true")));
  CHECK(backlog_contains(window, QStringLiteral("miss=0x0")));
  for (const char* name : {"nopoints", "noobject", "unknown", "limits", "sampling", "tolerance", "swatch", "protect",
                           "point",
                           "nox", "magicunknown", "magictolerance", "magicbool", "magicopacity", "inactive",
                           "inactivebrush"}) {
    CHECK(backlog_contains(window, QString::fromLatin1(name) + QStringLiteral("-refused=true")));
  }
  CHECK(!backlog_contains(window, QStringLiteral("no-throw")));
  // The script's edits are one undo step, and the tool the artist had is untouched.
  CHECK(require_canvas(window)->tool() == CanvasTool::Brush);
}

// Protect Foreground Color (GitHub issue 727 follow-up): the Background Eraser's check box.
// Off, a stroke that starts on red takes the dark red beside it too (it is within the
// Tolerance). Alt+click the dark red, check the box, and the same stroke keeps it.
void ui_background_eraser_protect_foreground_color() {
  const auto restorers = clear_eraser_settings();
  const QColor dark_red(150, 20, 20);
  {
    patchy::ui::MainWindow window;
    show_window(window);
    window.resize(1500, 780);  // the whole options bar, the new check box included
    QApplication::processEvents();
    auto* canvas = require_canvas(window);
    canvas->set_zoom(1.0);
    // White, a red block from (30, 40) to (109, 119) whose right half, from x = 70, is dark red.
    CHECK(run_script(window, QStringLiteral(R"JS(
      var doc = app.activeDocument;
      var layer = doc.addLayer('Picture');
      layer.fillRect(0, 0, 240, 160, '#ffffff');
      layer.fillRect(30, 40, 40, 80, '#d22828');
      layer.fillRect(70, 40, 40, 80, '#961414');
      doc.activeLayer = layer;
    )JS")));
    QApplication::processEvents();
    auto* document = patchy::ui::MainWindowTestAccess::document_for_canvas(window, canvas);
    CHECK(document != nullptr);
    if (document == nullptr) {
      return;
    }
    require_action(window, "toolBackgroundEraserAction")->trigger();
    QApplication::processEvents();
    auto* protect = window.findChild<QCheckBox*>(QStringLiteral("backgroundEraserProtectForegroundCheck"));
    auto* sampling = window.findChild<QComboBox*>(QStringLiteral("backgroundEraserSamplingCombo"));
    auto* tolerance = window.findChild<QSpinBox*>(QStringLiteral("backgroundEraserToleranceSpin"));
    auto* size = window.findChild<QSpinBox*>(QStringLiteral("brushSizeSpin"));
    auto* soft = window.findChild<QSpinBox*>(QStringLiteral("brushSoftnessSpin"));
    CHECK(protect != nullptr && sampling != nullptr && tolerance != nullptr && size != nullptr && soft != nullptr);
    if (protect == nullptr || sampling == nullptr || tolerance == nullptr || size == nullptr || soft == nullptr) {
      return;
    }
    // The check box: on the Background Eraser's bar, after Tolerance, off by default.
    CHECK(protect->isVisible() && !protect->isChecked());
    CHECK(protect->text() == QStringLiteral("Protect Foreground Color"));
    CHECK(!protect->toolTip().isEmpty() && !protect->toolTip().contains(QLatin1Char('%')));
    CHECK(protect->mapTo(&window, QPoint(0, 0)).x() > tolerance->mapTo(&window, QPoint(0, 0)).x());
    CHECK(!canvas->background_eraser_options().protect_foreground);
    require_action(window, "toolEraserAction")->trigger();
    QApplication::processEvents();
    CHECK(!protect->isVisible());
    require_action(window, "toolBackgroundEraserAction")->trigger();
    QApplication::processEvents();

    size->setValue(60);
    soft->setValue(0);
    sampling->setCurrentIndex(sampling->findText(QStringLiteral("Once")));
    QApplication::processEvents();
    const auto erase_across = [&] { stroke(*canvas, QPoint(45, 80), QPoint(95, 80)); };

    // Off: the stroke samples the red and erases the dark red with it.
    erase_across();
    CHECK(layer_alpha(*document, QPoint(50, 80)) == 0);
    CHECK(layer_alpha(*document, QPoint(90, 80)) == 0);
    patchy::ui::MainWindowTestAccess::undo(window);
    QApplication::processEvents();
    CHECK(layer_alpha(*document, QPoint(90, 80)) == 255);

    // On, with a foreground color far from both reds: nothing is protected, both go again.
    protect->setChecked(true);
    QApplication::processEvents();
    CHECK(canvas->background_eraser_options().protect_foreground);
    canvas->set_primary_color(QColor(20, 60, 220));
    erase_across();
    CHECK(layer_alpha(*document, QPoint(50, 80)) == 0);
    CHECK(layer_alpha(*document, QPoint(90, 80)) == 0);
    patchy::ui::MainWindowTestAccess::undo(window);
    QApplication::processEvents();
    CHECK(layer_alpha(*document, QPoint(90, 80)) == 255);

    // Alt+click picks the color to keep; the click erases nothing.
    const auto pick = canvas->widget_position_for_document_point(QPoint(90, 80));
    send_mouse(*canvas, QEvent::MouseButtonPress, pick, Qt::LeftButton, Qt::LeftButton, Qt::AltModifier);
    send_mouse(*canvas, QEvent::MouseButtonRelease, pick, Qt::LeftButton, Qt::NoButton, Qt::AltModifier);
    QApplication::processEvents();
    CHECK(color_close(canvas->primary_color(), dark_red, 1));
    CHECK(layer_alpha(*document, QPoint(90, 80)) == 255);

    // Now the red goes and the dark red stays, in one undo step.
    const auto depth_before = patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);
    erase_across();
    CHECK(layer_alpha(*document, QPoint(50, 80)) == 0);
    CHECK(layer_alpha(*document, QPoint(68, 80)) == 0);
    CHECK(layer_alpha(*document, QPoint(71, 80)) == 255);
    CHECK(layer_alpha(*document, QPoint(90, 80)) == 255);
    CHECK(is_color(layer_pixel(*document, QPoint(90, 80)), dark_red));
    CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == depth_before + 1);

    // The follow-up's AFTER shot: the picture alone at 400%, so the erased red shows as
    // transparency beside the dark red that stayed, under the bar with the new check box.
    CHECK(run_script(window, QStringLiteral(R"JS(
      app.activeDocument.layers.forEach(function (other) {
        if (other.name !== 'Picture') { other.visible = false; }
      });
      patchy.ui.zoomTo(80, 80, 400);
    )JS")));
    window.resize(1180, 640);
    QApplication::processEvents();
    CHECK(protect->isVisible());
    save_widget_artifact("issue_727_protect_foreground_color", window);

    patchy::ui::MainWindowTestAccess::save_tool_settings(window);
    CHECK(patchy::ui::app_settings().value(QStringLiteral("tools/backgroundEraserProtectForeground")).toBool());
  }
  {
    // "The next launch": the box comes back checked.
    auto&& [window, canvas] = ShownWindowFixture{};
    CHECK(canvas->background_eraser_options().protect_foreground);
    auto* protect = window.findChild<QCheckBox*>(QStringLiteral("backgroundEraserProtectForegroundCheck"));
    CHECK(protect != nullptr && protect->isChecked());
  }
  {
    // An unreadable value is the default, off.
    auto settings = patchy::ui::app_settings();
    settings.setValue(QStringLiteral("tools/backgroundEraserProtectForeground"), QStringLiteral("maybe"));
    settings.sync();
    auto&& [window, canvas] = ShownWindowFixture{};
    CHECK(!canvas->background_eraser_options().protect_foreground);
  }
}

// A 200x120 document for the modifier tests: "Back" is red with a green block from x = 120,
// and "Dot" above it holds one yellow square from (20, 20) to (39, 39). Back is active.
struct ModifierPicture {
  patchy::Document document{200, 120, patchy::PixelFormat::rgba8()};
  patchy::LayerId back_id{};
  patchy::LayerId dot_id{};

  ModifierPicture() {
    auto back = solid_pixels(200, 120, patchy::PixelFormat::rgba8(), kRed);
    fill_pixel_rect(back, QRect(120, 0, 80, 120), kGreen);
    back_id = document.add_pixel_layer("Back", std::move(back)).id();
    auto dot = solid_pixels(200, 120, patchy::PixelFormat::rgba8(), QColor(0, 0, 0, 0));
    fill_pixel_rect(dot, QRect(20, 20, 20, 20), kYellow);
    dot_id = document.add_pixel_layer("Dot", std::move(dot)).id();
    document.set_active_layer(back_id);
  }

  // How many pixels of a layer's own buffer are not fully opaque.
  [[nodiscard]] int pixels_not_opaque(patchy::LayerId id) const {
    const auto& pixels = document.find_layer(id)->pixels();
    int count = 0;
    for (int y = 0; y < pixels.height(); ++y) {
      for (int x = 0; x < pixels.width(); ++x) {
        count += pixels.pixel(x, y)[3] == 255 ? 0 : 1;
      }
    }
    return count;
  }
};

// Alt+click picks the foreground color in all three erasers, as in the plain Eraser (GitHub
// issue 727 follow-up): the eyedropper cursor shows while Alt is held, the click sets the
// foreground color, leaves the background color alone and erases nothing.
void ui_eraser_tools_alt_click_picks_the_foreground_color() {
  for (const auto tool : {CanvasTool::Eraser, CanvasTool::BackgroundEraser, CanvasTool::MagicEraser}) {
    ModifierPicture picture;
    patchy::ui::CanvasWidget canvas;
    canvas.resize(420, 320);
    canvas.set_document(&picture.document);
    canvas.set_zoom(1.0);
    canvas.set_selected_layer_ids({picture.back_id});
    canvas.show();
    QApplication::processEvents();
    canvas.set_tool(tool);
    canvas.set_brush_size(20);
    canvas.set_brush_softness(0);
    canvas.set_primary_color(QColor(1, 2, 3));
    canvas.set_secondary_color(QColor(250, 250, 250));
    const auto at = [&canvas](QPoint point) { return canvas.widget_position_for_document_point(point); };
    send_mouse(canvas, QEvent::MouseMove, at(QPoint(160, 60)), Qt::NoButton, Qt::NoButton);
    QApplication::processEvents();

    // The cursor: the eyedropper while Alt is held, the tool's own again on release.
    const auto eyedropper = patchy::ui::eyedropper_cursor().pixmap().toImage();
    send_key_release(canvas, Qt::Key_Alt, Qt::AltModifier);
    CHECK(canvas.cursor().pixmap().toImage() != eyedropper);
    send_key_press(canvas, Qt::Key_Alt);
    CHECK(canvas.cursor().pixmap().toImage() == eyedropper);
    CHECK(canvas.cursor().hotSpot() == QPoint(5, 27));
    send_key_release(canvas, Qt::Key_Alt, Qt::AltModifier);
    CHECK(canvas.cursor().pixmap().toImage() != eyedropper);

    // The click picks the green under the pointer into the foreground and erases nothing.
    send_mouse(canvas, QEvent::MouseButtonPress, at(QPoint(160, 60)), Qt::LeftButton, Qt::LeftButton,
               Qt::AltModifier);
    send_mouse(canvas, QEvent::MouseButtonRelease, at(QPoint(160, 60)), Qt::LeftButton, Qt::NoButton,
               Qt::AltModifier);
    QApplication::processEvents();
    CHECK(color_close(canvas.primary_color(), kGreen, 1));
    CHECK(canvas.secondary_color() == QColor(250, 250, 250));
    CHECK(picture.pixels_not_opaque(picture.back_id) == 0);
    CHECK(canvas.tool() == tool);

    // Without Alt the same click erases: the pick was the modifier, not the tool.
    send_mouse(canvas, QEvent::MouseButtonPress, at(QPoint(160, 60)), Qt::LeftButton, Qt::LeftButton);
    send_mouse(canvas, QEvent::MouseButtonRelease, at(QPoint(160, 60)), Qt::LeftButton, Qt::NoButton);
    QApplication::processEvents();
    CHECK(picture.pixels_not_opaque(picture.back_id) > 0);
  }
}

// Ctrl is the temporary Move in all three erasers (GitHub issue 727 follow-up): the four-way
// arrow shows while it is held, a Ctrl+drag moves the layer and erases nothing, and a
// Ctrl+click that never drags selects the layer under the pointer and erases nothing.
void ui_eraser_tools_ctrl_drag_moves_the_layer() {
  for (const auto tool : {CanvasTool::Eraser, CanvasTool::BackgroundEraser, CanvasTool::MagicEraser}) {
    ModifierPicture picture;
    auto& document = picture.document;
    patchy::ui::CanvasWidget canvas;
    canvas.resize(420, 320);
    canvas.set_document(&document);
    canvas.set_zoom(1.0);
    canvas.set_selected_layer_ids({picture.back_id});
    canvas.set_snap_enabled(false);
    canvas.show();
    QApplication::processEvents();
    canvas.set_tool(tool);
    canvas.set_brush_size(20);
    canvas.set_brush_softness(0);
    const auto at = [&canvas](QPoint point) { return canvas.widget_position_for_document_point(point); };
    const auto bounds = [&document](patchy::LayerId id) { return std::as_const(document).find_layer(id)->bounds(); };
    send_mouse(canvas, QEvent::MouseMove, at(QPoint(70, 80)), Qt::NoButton, Qt::NoButton);
    QApplication::processEvents();

    // The cursor: the four-way arrow while Ctrl is held.
    send_key_release(canvas, Qt::Key_Control, Qt::ControlModifier);
    CHECK(canvas.cursor().shape() != Qt::SizeAllCursor);
    send_key_press(canvas, Qt::Key_Control);
    CHECK(canvas.cursor().shape() == Qt::SizeAllCursor);
    send_key_release(canvas, Qt::Key_Control, Qt::ControlModifier);
    CHECK(canvas.cursor().shape() != Qt::SizeAllCursor);

    // Ctrl+drag: the active layer moves 10 px to the right and keeps every pixel.
    const auto before = bounds(picture.back_id);
    send_mouse(canvas, QEvent::MouseButtonPress, at(QPoint(70, 80)), Qt::LeftButton, Qt::LeftButton,
               Qt::ControlModifier);
    CHECK(canvas.cursor().shape() == Qt::SizeAllCursor);
    send_mouse(canvas, QEvent::MouseMove, at(QPoint(76, 80)), Qt::LeftButton, Qt::LeftButton, Qt::ControlModifier);
    send_mouse(canvas, QEvent::MouseMove, at(QPoint(80, 80)), Qt::LeftButton, Qt::LeftButton, Qt::ControlModifier);
    send_mouse(canvas, QEvent::MouseButtonRelease, at(QPoint(80, 80)), Qt::LeftButton, Qt::NoButton,
               Qt::ControlModifier);
    QApplication::processEvents();
    const auto after = bounds(picture.back_id);
    CHECK(after.x == before.x + 10);
    CHECK(after.y == before.y);
    CHECK(after.width == before.width && after.height == before.height);
    CHECK(picture.pixels_not_opaque(picture.back_id) == 0);
    CHECK(bounds(picture.dot_id).x == 0);  // the layer that was not selected stays
    CHECK(canvas.tool() == tool);

    // Ctrl+click on the yellow square selects its layer; nothing is erased on either layer.
    const auto dot_holes = picture.pixels_not_opaque(picture.dot_id);
    send_mouse(canvas, QEvent::MouseButtonPress, at(QPoint(30, 30)), Qt::LeftButton, Qt::LeftButton,
               Qt::ControlModifier);
    send_mouse(canvas, QEvent::MouseButtonRelease, at(QPoint(30, 30)), Qt::LeftButton, Qt::NoButton,
               Qt::ControlModifier);
    QApplication::processEvents();
    CHECK(std::as_const(document).active_layer_id() == picture.dot_id);
    CHECK(picture.pixels_not_opaque(picture.dot_id) == dot_holes);
    CHECK(picture.pixels_not_opaque(picture.back_id) == 0);

    // Ctrl+Alt is not the Move: no layer moves.
    const auto dot_before = bounds(picture.dot_id);
    send_mouse(canvas, QEvent::MouseButtonPress, at(QPoint(150, 100)), Qt::LeftButton, Qt::LeftButton,
               Qt::ControlModifier | Qt::AltModifier);
    send_mouse(canvas, QEvent::MouseMove, at(QPoint(160, 100)), Qt::LeftButton, Qt::LeftButton,
               Qt::ControlModifier | Qt::AltModifier);
    send_mouse(canvas, QEvent::MouseButtonRelease, at(QPoint(160, 100)), Qt::LeftButton, Qt::NoButton,
               Qt::ControlModifier | Qt::AltModifier);
    QApplication::processEvents();
    CHECK(bounds(picture.dot_id).x == dot_before.x && bounds(picture.back_id).x == after.x);
  }
}

// The status bar hints after the follow-up: the two brushes of the flyout share the painting
// tools' hint, and the Magic Eraser, a click tool, does not take it.
void ui_eraser_tool_hints_after_modifiers() {
  const auto restorers = clear_eraser_settings();
  patchy::ui::MainWindow window;
  show_window(window);
  window.resize(1500, 780);
  QApplication::processEvents();
  const auto hint = [&window](const char* action) {
    require_action(window, action)->trigger();
    QApplication::processEvents();
    return window.statusBar()->currentMessage();
  };
  const auto alt = patchy::ui::alt_key_name();
  CHECK(hint("toolEraserAction").startsWith(QStringLiteral("Eraser: Shift+click draws a line, ")));
  const auto background = hint("toolBackgroundEraserAction");
  CHECK(background.startsWith(QStringLiteral("Background Eraser: Shift+click draws a line, ")));
  CHECK(background.endsWith(alt + QStringLiteral("+right-drag changes size and softness.")));
  const auto magic = hint("toolMagicEraserAction");
  CHECK(!magic.contains(QStringLiteral("Shift+click draws a line")));
  CHECK(!magic.contains(QStringLiteral("right-drag")));
  require_action(window, "toolBrushAction")->trigger();
  QApplication::processEvents();
}

}  // namespace

std::vector<patchy::test::TestCase> eraser_tools_ui_tests() {
  return {
      {"ui_eraser_tools_alt_click_picks_the_foreground_color", ui_eraser_tools_alt_click_picks_the_foreground_color},
      {"ui_eraser_tools_ctrl_drag_moves_the_layer", ui_eraser_tools_ctrl_drag_moves_the_layer},
      {"ui_eraser_tool_hints_after_modifiers", ui_eraser_tool_hints_after_modifiers},
      {"ui_background_eraser_protect_foreground_color", ui_background_eraser_protect_foreground_color},
      {"ui_eraser_flyout_holds_the_three_erasers_and_shift_e_cycles",
       ui_eraser_flyout_holds_the_three_erasers_and_shift_e_cycles},
      {"ui_background_eraser_erases_the_sampled_color_in_one_undo_step",
       ui_background_eraser_erases_the_sampled_color_in_one_undo_step},
      {"ui_magic_eraser_click_erases_the_flood_region", ui_magic_eraser_click_erases_the_flood_region},
      {"ui_eraser_tools_follow_the_eraser_on_a_locked_background",
       ui_eraser_tools_follow_the_eraser_on_a_locked_background},
      {"ui_eraser_tool_options_survive_a_restart", ui_eraser_tool_options_survive_a_restart},
      {"ui_eraser_tools_erase_16_bit_layers_at_depth", ui_eraser_tools_erase_16_bit_layers_at_depth},
      {"ui_script_erase_background_and_magic_erase", ui_script_erase_background_and_magic_erase},
  };
}
