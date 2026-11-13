// Color Replacement tool (GitHub issue 720; docs/color-replacement.md): a brush in the Brush
// flyout that repaints the pixels matching a sampled color with the foreground color. Picked
// from the flyout it shows its options, a stroke recolors only the matching pixels in one undo
// step, the selection and the layer's alpha hold, the options survive a restart, 16-bit layers
// paint at depth, and scripts reach it through layer.replaceColor.
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
#include "ui_test_access.hpp"
#include "ui_test_support.hpp"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QImage>
#include <QMenu>
#include <QSpinBox>
#include <QToolButton>

#include <algorithm>
#include <array>
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
const QColor kBlue(30, 60, 220);

constexpr std::array<const char*, 6> kColorReplaceSettingsKeys{
    "tools/activeTool",          "tools/colorReplaceMode",      "tools/colorReplaceSampling",
    "tools/colorReplaceLimits",  "tools/colorReplaceTolerance", "tools/colorReplaceAntiAlias"};

std::list<SettingsValueRestorer> clear_color_replace_settings() {
  std::list<SettingsValueRestorer> restorers;
  auto settings = patchy::ui::app_settings();
  for (const char* key : kColorReplaceSettingsKeys) {
    restorers.emplace_back(QString::fromLatin1(key));
    settings.remove(QLatin1String(key));
  }
  settings.sync();
  return restorers;
}

// A 240x160 opaque layer: white, a red block from (30, 40) to (109, 119) and a green block from
// x = 150, made active. Returns false when the script failed.
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

bool is_bluish(QColor color) {
  return color.blue() > color.red() + 40 && color.blue() > color.green() + 40;
}

bool close_to(QColor actual, QColor expected, int tolerance = 2) {
  return std::abs(actual.red() - expected.red()) <= tolerance &&
         std::abs(actual.green() - expected.green()) <= tolerance &&
         std::abs(actual.blue() - expected.blue()) <= tolerance;
}

void stroke(patchy::ui::CanvasWidget& canvas, QPoint from, QPoint to) {
  drag_document_pixel_centers(canvas, {from, to}, 12);
  QApplication::processEvents();
}

void ui_color_replace_tool_recolors_from_the_brush_flyout() {
  const auto restorers = clear_color_replace_settings();
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_zoom(1.0);
  CHECK(add_test_picture(window));
  QApplication::processEvents();

  // The tool sits in the Brush button's flyout with the Brush, and picking it puts it on the button.
  auto* action = require_action(window, "toolColorReplacementAction");
  auto* brush_action = require_action(window, "toolBrushAction");
  auto* button = window.findChild<QToolButton*>(QStringLiteral("brushToolButton"));
  CHECK(button != nullptr && button->menu() != nullptr);
  if (button == nullptr || button->menu() == nullptr) {
    return;
  }
  CHECK(button->menu()->objectName() == QStringLiteral("brushToolMenu"));
  CHECK(button->menu()->actions().contains(action) && button->menu()->actions().contains(brush_action));
  CHECK(button->defaultAction() == brush_action);
  save_widget_artifact("ui_color_replace_before", window);
  action->trigger();
  QApplication::processEvents();
  CHECK(canvas->tool() == CanvasTool::ColorReplace);
  CHECK(button->defaultAction() == action);
  CHECK(action->isChecked());

  // Its options bar: the tip and size like the Brush, then its own controls with their defaults.
  auto* mode = window.findChild<QComboBox*>(QStringLiteral("colorReplaceModeCombo"));
  auto* sampling = window.findChild<QComboBox*>(QStringLiteral("colorReplaceSamplingCombo"));
  auto* limits = window.findChild<QComboBox*>(QStringLiteral("colorReplaceLimitsCombo"));
  auto* tolerance = window.findChild<QSpinBox*>(QStringLiteral("colorReplaceToleranceSpin"));
  auto* anti_alias = window.findChild<QCheckBox*>(QStringLiteral("colorReplaceAntiAliasCheck"));
  auto* size = window.findChild<QSpinBox*>(QStringLiteral("brushSizeSpin"));
  auto* soft = window.findChild<QSpinBox*>(QStringLiteral("brushSoftnessSpin"));
  auto* brush_mode = window.findChild<QComboBox*>(QStringLiteral("brushModeCombo"));
  auto* opacity = window.findChild<QSpinBox*>(QStringLiteral("brushOpacitySpin"));
  CHECK(mode != nullptr && sampling != nullptr && limits != nullptr && tolerance != nullptr && anti_alias != nullptr &&
        size != nullptr && soft != nullptr && brush_mode != nullptr && opacity != nullptr);
  if (mode == nullptr || sampling == nullptr || limits == nullptr || tolerance == nullptr || anti_alias == nullptr ||
      size == nullptr || soft == nullptr || brush_mode == nullptr || opacity == nullptr) {
    return;
  }
  for (QWidget* shown : std::initializer_list<QWidget*>{mode, sampling, limits, tolerance, anti_alias, size, soft}) {
    CHECK(shown->isVisible());
  }
  CHECK(!brush_mode->isVisible() && !opacity->isVisible());
  CHECK(mode->count() == 4 && mode->currentText() == QStringLiteral("Color"));
  CHECK(mode->itemText(0) == QStringLiteral("Hue") && mode->itemText(1) == QStringLiteral("Saturation") &&
        mode->itemText(3) == QStringLiteral("Luminosity"));
  CHECK(sampling->count() == 3 && sampling->currentText() == QStringLiteral("Continuous"));
  CHECK(limits->count() == 3 && limits->currentText() == QStringLiteral("Contiguous"));
  CHECK(tolerance->value() == 30 && tolerance->minimum() == 0 && tolerance->maximum() == 100);
  CHECK(anti_alias->isChecked());

  // One stroke across the red block with a blue foreground: the red turns blue, the white around it
  // and the green block the brush also reaches keep their colors.
  canvas->set_primary_color(kBlue);
  size->setValue(60);
  soft->setValue(0);
  QApplication::processEvents();
  CHECK(canvas->brush_size() == 60);
  CHECK(close_to(canvas_pixel(*canvas, QPoint(70, 80)), kRed));
  const auto depth_before = patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);
  stroke(*canvas, QPoint(40, 80), QPoint(128, 80));
  CHECK(is_bluish(canvas_pixel(*canvas, QPoint(70, 80))));
  CHECK(is_bluish(canvas_pixel(*canvas, QPoint(50, 70))));
  CHECK(is_bluish(canvas_pixel(*canvas, QPoint(100, 90))));
  CHECK(close_to(canvas_pixel(*canvas, QPoint(70, 44)), kRed));       // red the brush never reached
  CHECK(close_to(canvas_pixel(*canvas, QPoint(125, 80)), Qt::white));  // white under the brush
  CHECK(close_to(canvas_pixel(*canvas, QPoint(155, 80)), kGreen));     // under the last dabs, not red
  save_widget_artifact("ui_color_replace_after", window);

  // The whole stroke is one undo step.
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == depth_before + 1);
  patchy::ui::MainWindowTestAccess::undo(window);
  QApplication::processEvents();
  CHECK(close_to(canvas_pixel(*canvas, QPoint(70, 80)), kRed));
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == depth_before);

  // Background Swatch with Discontiguous: a stroke that starts on white replaces only the
  // background color (green here), wherever the brush reaches.
  canvas->set_secondary_color(kGreen);
  sampling->setCurrentIndex(sampling->findText(QStringLiteral("Background Swatch")));
  limits->setCurrentIndex(limits->findText(QStringLiteral("Discontiguous")));
  QApplication::processEvents();
  CHECK(canvas->color_replace_sampling() == patchy::ColorReplaceSampling::BackgroundSwatch);
  CHECK(canvas->color_replace_options().limits == patchy::ColorReplaceLimits::Discontiguous);
  stroke(*canvas, QPoint(100, 80), QPoint(180, 80));
  CHECK(is_bluish(canvas_pixel(*canvas, QPoint(170, 80))));
  CHECK(close_to(canvas_pixel(*canvas, QPoint(100, 80)), kRed));  // red under the brush, not the swatch color
  CHECK(close_to(canvas_pixel(*canvas, QPoint(125, 80)), Qt::white));

  // The Brush is still one click away in the same flyout, with its own bar.
  brush_action->trigger();
  QApplication::processEvents();
  CHECK(canvas->tool() == CanvasTool::Brush && button->defaultAction() == brush_action);
  CHECK(brush_mode->isVisible() && !mode->isVisible() && !tolerance->isVisible());
}

void ui_color_replace_honors_selection_alpha_and_locks() {
  const auto restorers = clear_color_replace_settings();
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_zoom(1.0);
  // A red layer that is partly transparent on its right third.
  CHECK(run_script(window, QStringLiteral(R"JS(
    var doc = app.activeDocument;
    var layer = doc.addLayer('Red');
    var W = 240, H = 160, data = new Uint8Array(W * H * 4);
    for (var y = 0; y < H; y++) {
      for (var x = 0; x < W; x++) {
        var i = (y * W + x) * 4;
        data[i] = 210; data[i + 1] = 40; data[i + 2] = 40; data[i + 3] = x < 160 ? 255 : 100;
      }
    }
    layer.setPixels({x: 0, y: 0, width: W, height: H, data: data.buffer});
    doc.activeLayer = layer;
  )JS")));
  auto* document = patchy::ui::MainWindowTestAccess::document_for_canvas(window, canvas);
  CHECK(document != nullptr && document->active_layer_id().has_value());
  if (document == nullptr || !document->active_layer_id().has_value()) {
    return;
  }
  const auto layer_id = *document->active_layer_id();
  const auto alpha_bytes = [&] {
    const auto& pixels = std::as_const(*document->find_layer(layer_id)).pixels();
    std::vector<std::uint8_t> alpha;
    alpha.reserve(static_cast<std::size_t>(pixels.width()) * static_cast<std::size_t>(pixels.height()));
    for (int y = 0; y < pixels.height(); ++y) {
      for (int x = 0; x < pixels.width(); ++x) {
        alpha.push_back(pixels.pixel(x, y)[3]);
      }
    }
    return alpha;
  };
  const auto layer_rgb = [&](QPoint document_point) {
    const auto* layer = std::as_const(*document).find_layer(layer_id);
    const auto bounds = layer->bounds();
    const auto* px = layer->pixels().pixel(document_point.x() - bounds.x, document_point.y() - bounds.y);
    return QColor(px[0], px[1], px[2]);
  };
  const auto alpha_before = alpha_bytes();
  {
    const auto* layer = std::as_const(*document).find_layer(layer_id);
    const auto bounds = layer->bounds();
    CHECK(bounds.x == 0 && bounds.y == 0 && layer->pixels().width() == 240 && layer->pixels().height() == 160);
    if (layer->pixels().width() != 240 || layer->pixels().height() != 160) {
      return;
    }
    CHECK(layer->pixels().pixel(190, 70)[3] == 100);
    CHECK(layer->pixels().pixel(60, 70)[3] == 255);
  }

  // A selection over the top half: the stroke changes only what is inside it.
  canvas->set_tool(CanvasTool::Marquee);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(0, 0)),
       canvas->widget_position_for_document_point(QPoint(240, 80)));
  QApplication::processEvents();
  CHECK(canvas->has_selection());
  require_action(window, "toolColorReplacementAction")->trigger();
  canvas->set_primary_color(kBlue);
  canvas->set_brush_size(50);
  canvas->set_brush_softness(0);
  stroke(*canvas, QPoint(30, 80), QPoint(210, 80));
  CHECK(is_bluish(layer_rgb(QPoint(60, 70))));
  CHECK(close_to(layer_rgb(QPoint(60, 90)), kRed, 0));   // below the selection
  CHECK(is_bluish(layer_rgb(QPoint(190, 70))));           // half-transparent pixels recolor too
  CHECK(close_to(layer_rgb(QPoint(190, 90)), kRed, 0));
  CHECK(alpha_bytes() == alpha_before);  // alpha never changes
  canvas->clear_selection();
  QApplication::processEvents();

  // A layer whose pixels are locked refuses the stroke and records nothing.
  const auto before_locked = layer_rgb(QPoint(60, 120));
  document->find_layer(layer_id)->set_lock_flags(patchy::kLayerLockImagePixels);
  const auto depth = patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);
  stroke(*canvas, QPoint(30, 120), QPoint(210, 120));
  CHECK(layer_rgb(QPoint(60, 120)) == before_locked);
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == depth);
  // Lock Transparent Pixels does not stop it: the tool never touches alpha anyway.
  document->find_layer(layer_id)->set_lock_flags(patchy::kLayerLockTransparentPixels);
  stroke(*canvas, QPoint(30, 120), QPoint(210, 120));
  CHECK(is_bluish(layer_rgb(QPoint(60, 120))));
  CHECK(alpha_bytes() == alpha_before);
}

void ui_color_replace_options_survive_a_restart() {
  const auto restorers = clear_color_replace_settings();
  {
    auto&& [window, canvas] = ShownWindowFixture{};
    require_action(window, "toolColorReplacementAction")->trigger();
    QApplication::processEvents();
    auto* mode = window.findChild<QComboBox*>(QStringLiteral("colorReplaceModeCombo"));
    auto* sampling = window.findChild<QComboBox*>(QStringLiteral("colorReplaceSamplingCombo"));
    auto* limits = window.findChild<QComboBox*>(QStringLiteral("colorReplaceLimitsCombo"));
    auto* tolerance = window.findChild<QSpinBox*>(QStringLiteral("colorReplaceToleranceSpin"));
    auto* anti_alias = window.findChild<QCheckBox*>(QStringLiteral("colorReplaceAntiAliasCheck"));
    CHECK(mode != nullptr && sampling != nullptr && limits != nullptr && tolerance != nullptr &&
          anti_alias != nullptr);
    if (mode == nullptr || sampling == nullptr || limits == nullptr || tolerance == nullptr || anti_alias == nullptr) {
      return;
    }
    mode->setCurrentIndex(mode->findText(QStringLiteral("Hue")));
    sampling->setCurrentIndex(sampling->findText(QStringLiteral("Once")));
    limits->setCurrentIndex(limits->findText(QStringLiteral("Find Edges")));
    tolerance->setValue(55);
    anti_alias->setChecked(false);
    QApplication::processEvents();
    // The controls reach the canvas at once.
    CHECK(canvas->color_replace_options().mode == patchy::ColorReplaceMode::Hue);
    CHECK(canvas->color_replace_options().limits == patchy::ColorReplaceLimits::FindEdges);
    CHECK(canvas->color_replace_options().tolerance == 55);
    CHECK(!canvas->color_replace_options().anti_alias);
    CHECK(canvas->color_replace_sampling() == patchy::ColorReplaceSampling::Once);
    patchy::ui::MainWindowTestAccess::save_tool_settings(window);
    auto settings = patchy::ui::app_settings();
    CHECK(settings.value(QStringLiteral("tools/activeTool")).toString() == QStringLiteral("tools.color_replacement"));
    CHECK(settings.value(QStringLiteral("tools/colorReplaceMode")).toString() == QStringLiteral("hue"));
    CHECK(settings.value(QStringLiteral("tools/colorReplaceSampling")).toString() == QStringLiteral("once"));
    CHECK(settings.value(QStringLiteral("tools/colorReplaceLimits")).toString() == QStringLiteral("findEdges"));
    CHECK(settings.value(QStringLiteral("tools/colorReplaceTolerance")).toInt() == 55);
    CHECK(!settings.value(QStringLiteral("tools/colorReplaceAntiAlias")).toBool());
  }
  {
    // "The next launch": a fresh window from the same settings store.
    auto&& [window, canvas] = ShownWindowFixture{};
    CHECK(canvas->tool() == CanvasTool::ColorReplace);
    auto* button = window.findChild<QToolButton*>(QStringLiteral("brushToolButton"));
    CHECK(button != nullptr && button->defaultAction() == require_action(window, "toolColorReplacementAction"));
    CHECK(canvas->color_replace_options().mode == patchy::ColorReplaceMode::Hue);
    CHECK(canvas->color_replace_options().limits == patchy::ColorReplaceLimits::FindEdges);
    CHECK(canvas->color_replace_options().tolerance == 55);
    CHECK(!canvas->color_replace_options().anti_alias);
    CHECK(canvas->color_replace_sampling() == patchy::ColorReplaceSampling::Once);
    auto* mode = window.findChild<QComboBox*>(QStringLiteral("colorReplaceModeCombo"));
    auto* sampling = window.findChild<QComboBox*>(QStringLiteral("colorReplaceSamplingCombo"));
    auto* limits = window.findChild<QComboBox*>(QStringLiteral("colorReplaceLimitsCombo"));
    auto* tolerance = window.findChild<QSpinBox*>(QStringLiteral("colorReplaceToleranceSpin"));
    auto* anti_alias = window.findChild<QCheckBox*>(QStringLiteral("colorReplaceAntiAliasCheck"));
    CHECK(mode != nullptr && mode->currentText() == QStringLiteral("Hue"));
    CHECK(sampling != nullptr && sampling->currentText() == QStringLiteral("Once"));
    CHECK(limits != nullptr && limits->currentText() == QStringLiteral("Find Edges"));
    CHECK(tolerance != nullptr && tolerance->value() == 55);
    CHECK(anti_alias != nullptr && !anti_alias->isChecked());
  }
  {
    // Unknown tokens and an out-of-range tolerance fall back to the defaults.
    auto settings = patchy::ui::app_settings();
    settings.setValue(QStringLiteral("tools/colorReplaceMode"), QStringLiteral("multiply"));
    settings.setValue(QStringLiteral("tools/colorReplaceSampling"), QStringLiteral("twice"));
    settings.setValue(QStringLiteral("tools/colorReplaceLimits"), 7);
    settings.setValue(QStringLiteral("tools/colorReplaceTolerance"), QStringLiteral("lots"));
    settings.setValue(QStringLiteral("tools/colorReplaceAntiAlias"), QStringLiteral("maybe"));
    settings.sync();
    auto&& [window, canvas] = ShownWindowFixture{};
    CHECK(canvas->color_replace_options() == patchy::ColorReplaceOptions{});
    CHECK(canvas->color_replace_sampling() == patchy::ColorReplaceSampling::Continuous);
  }
}

// A 16-bit layer is painted at its own depth: values between 8-bit steps survive outside the
// stroke and the recolored pixels are not rounded to bytes.
void ui_color_replace_paints_16_bit_layers_at_depth() {
  constexpr int kWidth = 200;
  constexpr int kHeight = 120;
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
  canvas.set_tool(CanvasTool::ColorReplace);
  canvas.set_primary_color(kBlue);
  canvas.set_brush_size(40);
  canvas.set_brush_softness(0);
  const auto value_at = [&](int x, int y) {
    const auto& deep = std::as_const(*document.find_layer(layer_id)).pixels();
    return patchy::load_pixel(deep.format(), deep.pixel(x, y));
  };
  const auto red_before = value_at(60, 60);
  const auto untouched_before = value_at(60, 110);
  const auto green_before = value_at(130, 60);
  stroke(canvas, QPoint(30, 60), QPoint(110, 60));
  const auto& format = std::as_const(*document.find_layer(layer_id)).pixels().format();
  CHECK(format.bit_depth == patchy::BitDepth::UInt16);
  const auto painted = value_at(60, 60);
  CHECK(painted[2] > painted[0] + 40.0F);         // blue now
  CHECK(painted[3] == red_before[3]);             // alpha untouched
  CHECK(value_at(60, 110) == untouched_before);   // off-grid value outside the stroke survives
  CHECK(value_at(130, 60) == green_before);       // green under the last dabs is not the sampled red
  const auto expected = patchy::color_replace_pixel_deep(red_before, {30.0F, 60.0F, 220.0F},
                                                         patchy::ColorReplaceMode::Color, 1.0F,
                                                         patchy::DeepDomain::Encoded);
  for (std::size_t channel = 0; channel < 3; ++channel) {
    CHECK(std::abs(painted[channel] - expected[channel]) < 0.01F);
  }
}

// A shaded red apple with a green leaf just above it, on a pale backdrop. With the defaults
// (Color, Continuous, Contiguous, 30%), a few passes over the apple turn it blue and keep its
// shading: each pixel's lightness stays, and the leaf, the shadow and the backdrop under the
// brush are untouched.
void ui_color_replace_recolors_a_shaded_object_and_keeps_its_shading() {
  const auto restorers = clear_color_replace_settings();
  patchy::ui::MainWindow window;
  show_window(window);
  CHECK(run_script(window, QStringLiteral(R"JS(
    var W = 420, H = 300;
    var doc = app.newDocument(W, H, {name: 'Apple'});
    var layer = doc.addLayer('Apple');
    var data = new Uint8Array(W * H * 4);
    var cx = 210, cy = 165, r = 100;
    for (var y = 0; y < H; y++) {
      for (var x = 0; x < W; x++) {
        var t = y / H;
        var R = 232 - 20 * t, G = 235 - 18 * t, B = 240 - 14 * t;
        var sx = (x - cx - 20) / 125, sy = (y - (cy + r - 4)) / 20;
        var s = sx * sx + sy * sy;
        if (s < 1) { var k = 0.80 + 0.20 * s; R *= k; G *= k; B *= k; }
        var lx = (x - (cx + 36)) / 32, ly = (y - (cy - r - 20)) / 12;
        if (lx * lx + ly * ly < 1) { R = 62; G = 150; B = 64; }
        var dx = (x - cx) / r, dy = (y - cy) / r, d2 = dx * dx + dy * dy;
        if (d2 < 1) {
          var nz = Math.sqrt(1 - d2);
          var light = Math.max(0, -0.45 * dx - 0.55 * dy + 0.70 * nz);
          var shade = 0.38 + 0.62 * light;
          var spec = Math.pow(light, 24) * 0.6;
          R = 214 * shade + (255 - 214 * shade) * spec;
          G = 34 * shade + (255 - 34 * shade) * spec;
          B = 44 * shade + (255 - 44 * shade) * spec;
        }
        var i = (y * W + x) * 4;
        data[i] = R; data[i + 1] = G; data[i + 2] = B; data[i + 3] = 255;
      }
    }
    layer.setPixels({x: 0, y: 0, width: W, height: H, data: data.buffer});
    doc.activeLayer = layer;
  )JS")));
  QApplication::processEvents();
  auto* canvas = require_canvas(window);
  CHECK(patchy::ui::MainWindowTestAccess::document(window).width() == 420);
  canvas->set_zoom(1.0);
  QApplication::processEvents();
  require_action(window, "toolColorReplacementAction")->trigger();
  canvas->set_primary_color(kBlue);
  auto* size = window.findChild<QSpinBox*>(QStringLiteral("brushSizeSpin"));
  auto* soft = window.findChild<QSpinBox*>(QStringLiteral("brushSoftnessSpin"));
  CHECK(size != nullptr && soft != nullptr);
  if (size == nullptr || soft == nullptr) {
    return;
  }
  size->setValue(90);
  soft->setValue(0);
  QApplication::processEvents();
  CHECK(canvas->color_replace_options() == patchy::ColorReplaceOptions{});
  CHECK(canvas->color_replace_sampling() == patchy::ColorReplaceSampling::Continuous);

  const std::vector<QPoint> probes{QPoint(210, 165), QPoint(150, 120), QPoint(270, 215), QPoint(130, 190),
                                   QPoint(285, 150), QPoint(210, 250), QPoint(215, 80)};
  const auto lightness = [](QColor color) {
    return 0.30 * color.red() + 0.59 * color.green() + 0.11 * color.blue();
  };
  std::vector<QColor> before;
  for (const auto probe : probes) {
    before.push_back(canvas_pixel(*canvas, probe));
    CHECK(before.back().red() > before.back().blue() + 20);  // red to start with
  }
  const auto leaf_before = canvas_pixel(*canvas, QPoint(246, 53));
  const auto backdrop_before = canvas_pixel(*canvas, QPoint(60, 60));
  const auto shadow_before = canvas_pixel(*canvas, QPoint(270, 262));
  const auto highlight_before = canvas_pixel(*canvas, QPoint(165, 110));
  CHECK(leaf_before.green() > leaf_before.red() + 50);
  save_widget_artifact("ui_color_replace_apple_before", window);

  // One stroke that scrubs back and forth over the ball, its center always on the ball:
  // Continuous samples the stroke-start color under the center of every dab, so the dabs
  // between them take in the lit side, the highlight and the dark side.
  const auto depth_before = patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);
  drag_document_pixel_centers(*canvas,
                              {QPoint(160, 95), QPoint(260, 95), QPoint(280, 110), QPoint(140, 110), QPoint(125, 165),
                               QPoint(295, 165), QPoint(270, 225), QPoint(150, 225)},
                              10);
  QApplication::processEvents();
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == depth_before + 1);
  // Nearly every pixel of the ball is blue now; what is left is a few edge pixels the
  // anti-alias fringe only tinted.
  int red_left = 0;
  {
    const auto& document = patchy::ui::MainWindowTestAccess::document(window);
    const auto* layer = document.active_layer_id().has_value() ? document.find_layer(*document.active_layer_id())
                                                               : nullptr;
    CHECK(layer != nullptr && layer->pixels().width() == 420 && layer->pixels().height() == 300);
    if (layer == nullptr || layer->pixels().width() != 420 || layer->pixels().height() != 300) {
      return;
    }
    QRect left_box;
    for (int y = 65; y <= 265; ++y) {
      for (int x = 110; x <= 310; ++x) {
        if ((x - 210) * (x - 210) + (y - 165) * (y - 165) >= 99 * 99) {
          continue;
        }
        const auto* px = layer->pixels().pixel(x, y);
        if (px[0] > px[2] + 20) {
          ++red_left;
          left_box = left_box.united(QRect(x, y, 1, 1));
        }
      }
    }
    if (red_left > 0) {
      std::printf("[INFO] %d of about 30800 ball pixels are still red, within %d,%d %dx%d\n", red_left,
                  left_box.x(), left_box.y(), left_box.width(), left_box.height());
    }
  }
  CHECK(red_left < 154);  // under half a percent
  for (std::size_t index = 0; index < probes.size(); ++index) {
    const auto after = canvas_pixel(*canvas, probes[index]);
    CHECK(is_bluish(after));
    // Color mode keeps each pixel's lightness, so the apple keeps its shading.
    CHECK(std::abs(lightness(after) - lightness(before[index])) <= 6.0);
  }
  CHECK(canvas_pixel(*canvas, QPoint(246, 53)) == leaf_before);       // under the brush, not red
  CHECK(canvas_pixel(*canvas, QPoint(60, 60)) == backdrop_before);
  CHECK(canvas_pixel(*canvas, QPoint(270, 262)) == shadow_before);    // under the brush, not red
  // The near-white highlight takes a pale blue and stays as bright; the dark rim turns blue too.
  const auto highlight_after = canvas_pixel(*canvas, QPoint(165, 110));
  CHECK(highlight_after.blue() > highlight_after.red() + 20);
  CHECK(std::abs(lightness(highlight_after) - lightness(highlight_before)) <= 6.0);
  for (const auto rim : {QPoint(305, 140), QPoint(300, 200), QPoint(112, 165), QPoint(210, 262)}) {
    const auto color = canvas_pixel(*canvas, rim);
    CHECK(color.blue() > color.red() + 20);
  }
  save_widget_artifact("ui_color_replace_apple_after", window);
}

void ui_script_replace_color_strokes_and_validation() {
  const auto restorers = clear_color_replace_settings();
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
    var red = at(70, 80);
    var r = layer.replaceColor({points: [{x: 40, y: 80}, {x: 128, y: 80}], color: '#1e3cdc', size: 60});
    console.log('stroke=' + (r.width > 0 && r.height > 0) + ' inside=' + (r.x >= 30 && r.x + r.width <= 111));
    var p = at(70, 80).split(',');
    console.log('blue=' + (Number(p[2]) > Number(p[0]) + 40) + ' alpha=' + p[3]);
    console.log('white=' + at(125, 80) + ' green=' + at(155, 80) + ' far=' + (at(70, 44) === red));
    // The background color through the whole layer, with a brush that never centers on it.
    var g = layer.replaceColor({points: [{x: 130, y: 80}, {x: 140, y: 80}], color: '#1e3cdc', size: 80,
                                sampling: 'background', backgroundColor: '#28aa3c', limits: 'discontiguous',
                                mode: 'hue', tolerance: 10, antiAlias: false, hardness: 100});
    var q = at(160, 80).split(',');
    console.log('swatch=' + (Number(q[2]) > Number(q[0]) + 40) + ' box=' + (g.x >= 150));
    // Nothing matches: no error, an empty box.
    var none = layer.replaceColor({points: [{x: 125, y: 20}], color: '#1e3cdc', size: 10, sampling: 'background',
                                   backgroundColor: '#000000'});
    console.log('none=' + none.width + 'x' + none.height);
    var refuse = function (name, options) {
      try {
        layer.replaceColor(options);
        console.log(name + '-no-throw');
      } catch (e) {
        console.log(name + '-refused=' + (e.message.length > 0));
      }
    };
    refuse('nopoints', {color: '#1e3cdc'});
    refuse('nocolor', {points: [{x: 1, y: 1}]});
    refuse('unknown', {points: [{x: 1, y: 1}], color: '#1e3cdc', opacity: 50});
    refuse('mode', {points: [{x: 1, y: 1}], color: '#1e3cdc', mode: 'multiply'});
    refuse('limits', {points: [{x: 1, y: 1}], color: '#1e3cdc', limits: 'findedges'});
    refuse('tolerance', {points: [{x: 1, y: 1}], color: '#1e3cdc', tolerance: 101});
    refuse('swatch', {points: [{x: 1, y: 1}], color: '#1e3cdc', sampling: 'background'});
    refuse('point', {points: [{x: 1}], color: '#1e3cdc'});
    refuse('antialias', {points: [{x: 1, y: 1}], color: '#1e3cdc', antiAlias: 1});
  )JS")));
  CHECK(backlog_contains(window, QStringLiteral("stroke=true inside=true")));
  CHECK(backlog_contains(window, QStringLiteral("blue=true alpha=255")));
  CHECK(backlog_contains(window, QStringLiteral("white=255,255,255,255 green=40,170,60,255 far=true")));
  CHECK(backlog_contains(window, QStringLiteral("swatch=true box=true")));
  CHECK(backlog_contains(window, QStringLiteral("none=0x0")));
  for (const char* name : {"nopoints", "nocolor", "unknown", "mode", "limits", "tolerance", "swatch", "point",
                           "antialias"}) {
    CHECK(backlog_contains(window, QString::fromLatin1(name) + QStringLiteral("-refused=true")));
  }
  CHECK(!backlog_contains(window, QStringLiteral("no-throw")));
  // The script's strokes are one undo step, and the tool the artist had is untouched.
  CHECK(require_canvas(window)->tool() == CanvasTool::Brush);
  patchy::ui::MainWindowTestAccess::undo(window);
  QApplication::processEvents();
  CHECK(close_to(canvas_pixel(*require_canvas(window), QPoint(70, 80)), kRed));
}

}  // namespace

std::vector<patchy::test::TestCase> color_replace_ui_tests() {
  return {
      {"ui_color_replace_tool_recolors_from_the_brush_flyout", ui_color_replace_tool_recolors_from_the_brush_flyout},
      {"ui_color_replace_honors_selection_alpha_and_locks", ui_color_replace_honors_selection_alpha_and_locks},
      {"ui_color_replace_options_survive_a_restart", ui_color_replace_options_survive_a_restart},
      {"ui_color_replace_paints_16_bit_layers_at_depth", ui_color_replace_paints_16_bit_layers_at_depth},
      {"ui_color_replace_recolors_a_shaded_object_and_keeps_its_shading",
       ui_color_replace_recolors_a_shaded_object_and_keeps_its_shading},
      {"ui_script_replace_color_strokes_and_validation", ui_script_replace_color_strokes_and_validation},
  };
}
