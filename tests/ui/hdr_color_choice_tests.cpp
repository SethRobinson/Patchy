// Color choice in 32-bit documents (docs/high-bit-depth.md, "Color choice at 32 bits"): the
// Foreground and Background Color windows and the Color panel show the color's linear value
// and Intensity in stops while the active document is 32-bit, painting carries the
// Intensity into 32-bit layers, and the Eyedropper keeps a picked value above 1.0.
#include "core/document.hpp"
#include "core/document_depth.hpp"
#include "core/hdr_color.hpp"
#include "core/layer_tree.hpp"
#include "core/pixel_depth.hpp"
#include "ui/canvas_widget.hpp"
#include "ui/color_dock_panel.hpp"
#include "ui/color_panel.hpp"
#include "ui/hdr_color_controls.hpp"
#include "ui/main_window.hpp"
#include "ui/tool_palette_footer.hpp"
#include <QApplication>
#include <QColor>
#include <QDialog>
#include <QDir>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QTimer>
#include <QScopeGuard>
#include <QString>
#include <cmath>
#include <optional>
#include <utility>
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
using patchy::ui::HdrColorControls;
using patchy::ui::MainWindowTestAccess;

ColorDockPanel* open_color_panel(patchy::ui::MainWindow& window) {
  MainWindowTestAccess::show_panel(window, QStringLiteral("colorDock"));
  QApplication::processEvents();
  QApplication::processEvents();
  return window.findChild<ColorDockPanel*>(QStringLiteral("colorPanel"));
}

QDialog* open_foreground_window(patchy::ui::MainWindow& window, ColorDockPanel& panel) {
  panel.foreground_chip()->click();
  QApplication::processEvents();
  return window.findChild<QDialog*>(QStringLiteral("patchyColorDialog"));
}

HdrColorControls* window_hdr(QDialog& dialog) { return dialog.findChild<HdrColorControls*>(); }

bool shown_in(const QWidget* widget, const QWidget* ancestor) {
  return widget != nullptr && widget->isVisibleTo(ancestor);
}

void save_capture(QWidget& widget, const QString& name) {
  QDir().mkpath(QStringLiteral("test-artifacts/hdr-color"));
  widget.grab().save(QStringLiteral("test-artifacts/hdr-color/%1.png").arg(name));
}

void set_panel_rgb(ColorDockPanel& panel, int red, int green, int blue) {
  panel.set_view(ColorDockPanel::View::RgbSliders);
  panel.set_channel(ColorDockPanel::Channel::Red, red);
  panel.set_channel(ColorDockPanel::Channel::Green, green);
  panel.set_channel(ColorDockPanel::Channel::Blue, blue);
  QApplication::processEvents();
}

const patchy::Layer* active_layer(patchy::ui::MainWindow& window) {
  const auto& document = std::as_const(MainWindowTestAccess::document(window));
  const auto id = document.active_layer_id();
  return id.has_value() ? document.find_layer(*id) : nullptr;
}

double decoded(int value) { return patchy::srgb_decode(static_cast<double>(value) / 255.0); }

int encoded(double linear) { return static_cast<int>(std::lround(patchy::srgb_encode(linear) * 255.0)); }

// The window and the panel show their 32-bit controls only in a 32-bit document; 8-bit and
// 16-bit documents look as before. Captures (test-artifacts/hdr-color/*.png) of both in an
// 8-bit document and in a 32-bit one at +1.7 stops.
void ui_hdr_color_choice_captures() {
  patchy::set_deep_editing_override(true);
  const auto restore = qScopeGuard([] { patchy::set_deep_editing_override(std::nullopt); });
  SettingsValueRestorer view_setting(QStringLiteral("colorPanel/view"));
  SettingsValueRestorer foreground_setting(QStringLiteral("tools/foregroundColor"));
  for (const int bits : {8, 16, 32}) {
    patchy::ui::MainWindow window;
    show_window(window);
    window.resize(1400, 900);
    CHECK(run_script(window, QStringLiteral("var doc = app.newDocument(64, 48); doc.convertBitDepth(%1);").arg(bits)));
    auto* canvas = require_canvas(window);
    auto* panel = open_color_panel(window);
    CHECK(panel != nullptr);
    if (panel == nullptr) {
      return;
    }
    set_panel_rgb(*panel, 149, 33, 33);
    CHECK(canvas->primary_color().rgb() == QColor(149, 33, 33).rgb());
    auto* dialog = open_foreground_window(window, *panel);
    CHECK(dialog != nullptr);
    if (dialog == nullptr) {
      return;
    }
    auto* hdr = window_hdr(*dialog);
    CHECK(hdr != nullptr);
    if (hdr == nullptr) {
      return;
    }
    const bool deep = bits == 32;
    CHECK(shown_in(hdr, dialog) == deep);
    CHECK(shown_in(panel->hdr_controls(), panel) == deep);
    if (deep) {
      hdr->intensity_spin()->setValue(1.7);
      QApplication::processEvents();
      CHECK(std::abs(canvas->paint_intensity().foreground - 1.7) < 1e-9);
      CHECK(std::abs(panel->hdr_controls()->stops() - 1.7) < 1e-9);
      const auto expected_red = decoded(149) * std::exp2(1.7);
      CHECK(std::abs(hdr->linear_spin(0)->value() - expected_red) < 1e-4);
      CHECK(!panel->hdr_controls()->linear_label()->text().isEmpty());
      // The window grew to hold them.
      CHECK(dialog->height() >= dialog->sizeHint().height());
    }
    QApplication::processEvents();
    if (bits != 16) {
      save_capture(*dialog, QStringLiteral("dialog-%1bit").arg(bits));
      save_capture(*panel, QStringLiteral("panel-%1bit").arg(bits));
    }
    dialog->close();
  }
}

// The Intensity set in the window reaches the canvas, the panel follows, a new base color
// keeps it, Fill paints it above 1.0, a typed linear value splits into base and stops,
// Swap carries it and Default Colors clears it; an 8-bit document hides the controls again.
void ui_hdr_color_intensity_paints_and_follows() {
  patchy::set_deep_editing_override(true);
  const auto restore = qScopeGuard([] { patchy::set_deep_editing_override(std::nullopt); });
  SettingsValueRestorer view_setting(QStringLiteral("colorPanel/view"));
  SettingsValueRestorer foreground_setting(QStringLiteral("tools/foregroundColor"));
  SettingsValueRestorer background_setting(QStringLiteral("tools/backgroundColor"));
  patchy::ui::MainWindow window;
  show_window(window);
  CHECK(run_script(window, QStringLiteral("var doc = app.newDocument(32, 24); doc.convertBitDepth(32);")));
  auto* canvas = require_canvas(window);
  auto* panel = open_color_panel(window);
  if (panel == nullptr) {
    CHECK(false);
    return;
  }
  set_panel_rgb(*panel, 149, 33, 33);
  auto* dialog = open_foreground_window(window, *panel);
  auto* hdr = dialog != nullptr ? window_hdr(*dialog) : nullptr;
  CHECK(hdr != nullptr && shown_in(hdr, dialog));
  if (hdr == nullptr) {
    return;
  }
  hdr->intensity_spin()->setValue(2.0);
  QApplication::processEvents();
  CHECK(canvas->paint_intensity().foreground == 2.0);
  CHECK(panel->hdr_controls()->stops() == 2.0);

  // A preview swatch moves the Intensity by its offset.
  auto* strip = hdr->preview_strip();
  const auto minus_one = strip->swatch_rect(-1).center();
  send_mouse(*strip, QEvent::MouseButtonPress, minus_one, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*strip, QEvent::MouseButtonRelease, minus_one, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(canvas->paint_intensity().foreground == 1.0);
  CHECK(strip->swatch_color(1).red() >= strip->swatch_color(0).red());
  hdr->intensity_spin()->setValue(2.0);
  QApplication::processEvents();

  // A new base color from the panel keeps the Intensity.
  panel->set_channel(ColorDockPanel::Channel::Green, 60);
  QApplication::processEvents();
  CHECK(canvas->primary_color().rgb() == QColor(149, 60, 33).rgb());
  CHECK(canvas->paint_intensity().foreground == 2.0);
  CHECK(hdr->stops() == 2.0);

  // Fill paints four times the decoded color: values above 1.0.
  require_action(window, "layerFillForegroundAction")->trigger();
  QApplication::processEvents();
  const auto* layer = active_layer(window);
  CHECK(layer != nullptr);
  if (layer != nullptr) {
    const auto& pixels = layer->pixels();
    CHECK(pixels.format().bit_depth == patchy::BitDepth::Float32);
    const auto value = patchy::load_pixel(pixels.format(), pixels.pixel(4, 4));
    CHECK(std::abs(value[0] / 255.0 - decoded(149) * 4.0) < 1e-4);
    CHECK(std::abs(value[1] / 255.0 - decoded(60) * 4.0) < 1e-4);
    CHECK(value[0] > 255.0F);
  }

  // A typed linear value: red 2.0 is base 255 at +1 stop.
  hdr->linear_spin(0)->setValue(2.0);
  QApplication::processEvents();
  CHECK(canvas->primary_color().red() == 255);
  CHECK(std::abs(canvas->paint_intensity().foreground - 1.0) < 1e-9);
  CHECK(std::abs(panel->hdr_controls()->stops() - 1.0) < 1e-9);
  auto* picker = dialog->findChild<patchy::ui::PatchyColorPicker*>(QStringLiteral("patchyAdvancedColorPicker"));
  CHECK(picker != nullptr && picker->currentColor().red() == 255);

  // Swap carries each side's Intensity; Default Colors clears both.
  require_action(window, "colorSwapAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->paint_intensity().foreground == 0.0);
  CHECK(std::abs(canvas->paint_intensity().background - 1.0) < 1e-9);
  CHECK(canvas->secondary_color().red() == 255);
  require_action(window, "colorSwapAction")->trigger();
  QApplication::processEvents();
  CHECK(std::abs(canvas->paint_intensity().foreground - 1.0) < 1e-9);
  require_action(window, "colorDefaultAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->paint_intensity() == patchy::PaintIntensity{});
  CHECK(hdr->stops() == 0.0);

  // An 8-bit document hides both again.
  CHECK(run_script(window, QStringLiteral("app.newDocument(16, 16);")));
  QApplication::processEvents();
  CHECK(!shown_in(hdr, dialog));
  CHECK(!shown_in(panel->hdr_controls(), panel));
  dialog->close();
}

// The Eyedropper on a 32-bit document keeps a value above 1.0: the base color and the
// Intensity rebuild it, through All Layers and Current Layer alike.
void ui_hdr_eyedropper_keeps_32_bit_values() {
  patchy::Document document(48, 16, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Light", solid_pixels(48, 16, patchy::PixelFormat::rgba8(), QColor(10, 10, 10)));
  patchy::convert_document_depth(document, patchy::BitDepth::Float32);
  {
    auto& layer = *document.find_layer(document.layers().front().id());
    auto& pixels = layer.pixels();
    for (int y = 0; y < pixels.height(); ++y) {
      for (int x = 0; x < pixels.width(); ++x) {
        patchy::store_pixel(pixels.format(), pixels.pixel(x, y), {3.0F * 255.0F, 0.75F * 255.0F, 0.375F * 255.0F, 255.0F});
      }
    }
  }
  CHECK(document.color_state().bit_depth == patchy::BitDepth::Float32);
  patchy::ui::CanvasWidget canvas;
  canvas.resize(192, 64);
  canvas.set_document(&document);
  canvas.set_tool(patchy::ui::CanvasTool::Eyedropper);
  canvas.show();
  QApplication::processEvents();
  const auto point = canvas.widget_position_for_document_point(QPoint(30, 8));
  for (const auto mode : {patchy::SampleLayers::AllLayers, patchy::SampleLayers::CurrentLayer}) {
    canvas.set_primary_color(QColor(1, 2, 3));
    CHECK(canvas.paint_intensity().foreground == 0.0);
    canvas.set_eyedropper_sample_layers(mode);
    send_mouse(canvas, QEvent::MouseButtonPress, point, Qt::LeftButton, Qt::LeftButton);
    send_mouse(canvas, QEvent::MouseButtonRelease, point, Qt::LeftButton, Qt::NoButton);
    QApplication::processEvents();
    CHECK(color_close(canvas.primary_color(), QColor(255, encoded(0.25), encoded(0.125)), 1));
    CHECK(std::abs(canvas.paint_intensity().foreground - std::log2(3.0)) < 1e-4);
    const auto rebuilt = patchy::hdr_linear_color(
        {{static_cast<std::uint8_t>(canvas.primary_color().red()), static_cast<std::uint8_t>(canvas.primary_color().green()),
          static_cast<std::uint8_t>(canvas.primary_color().blue())},
         canvas.paint_intensity().foreground});
    CHECK(std::abs(rebuilt[0] - 3.0) < 1e-3);
    CHECK(std::abs(rebuilt[1] - 0.75) < 0.02);
  }
  canvas.set_document(nullptr);
}

// Gradients at 32 bits (follow-up): the Edit Gradient Stops dialog shows the current stop's
// Intensity only in a 32-bit document and stores an edit on that stop; the Gradient tool's
// Foreground to Background runs above 1.0 from a +2 stop foreground, and the Eyedropper reads
// it back. Captures: gradient-editor-32bit.png and gradient-32bit-eyedropper.png.
void ui_hdr_gradient_stops_carry_intensity() {
  patchy::set_deep_editing_override(true);
  const auto restore = qScopeGuard([] { patchy::set_deep_editing_override(std::nullopt); });
  SettingsValueRestorer view_setting(QStringLiteral("colorPanel/view"));
  SettingsValueRestorer foreground_setting(QStringLiteral("tools/foregroundColor"));
  SettingsValueRestorer background_setting(QStringLiteral("tools/backgroundColor"));
  SettingsValueRestorer use_custom(QStringLiteral("tools/gradientUseCustomStops"));
  SettingsValueRestorer saved_stops(QStringLiteral("tools/gradientStops"));
  SettingsValueRestorer saved_method(QStringLiteral("tools/gradientMethod"));
  SettingsValueRestorer saved_reverse(QStringLiteral("tools/gradientReverse"));
  SettingsValueRestorer saved_opacity(QStringLiteral("tools/gradientOpacity"));
  for (const int bits : {8, 32}) {
    patchy::ui::MainWindow window;
    show_window(window);
    window.resize(1200, 760);
    CHECK(run_script(window, QStringLiteral("var doc = app.newDocument(240, 120); doc.convertBitDepth(%1);").arg(bits)));
    auto* canvas = require_canvas(window);
    auto* panel = open_color_panel(window);
    if (panel == nullptr) {
      CHECK(false);
      return;
    }
    set_panel_rgb(*panel, 255, 0, 0);
    canvas->set_secondary_color(Qt::white);
    canvas->paint_intensity() = {2.0, 0.0};
    canvas->set_gradient_stops(std::nullopt);
    canvas->set_tool(patchy::ui::CanvasTool::Gradient);
    QApplication::processEvents();
    const bool deep = bits == 32;

    // The dialog: the 32-bit row only at 32 bits, showing and editing the current stop.
    bool checked = false;
    QTimer::singleShot(0, [&] {
      auto* dialog = find_top_level_dialog(QStringLiteral("gradientStopsDialog"));
      CHECK(dialog != nullptr);
      if (dialog == nullptr) {
        return;
      }
      auto* hdr = dialog->findChild<HdrColorControls*>(QStringLiteral("gradientStopHdrControls"));
      auto* table = dialog->findChild<QTableWidget*>(QStringLiteral("gradientStopsTable"));
      CHECK(hdr != nullptr && table != nullptr);
      if (hdr != nullptr && table != nullptr) {
        CHECK(shown_in(hdr, dialog) == deep);
        table->setCurrentCell(0, 0);
        QApplication::processEvents();
        CHECK(hdr->stops() == 2.0);
        if (deep) {
          table->setCurrentCell(1, 0);
          QApplication::processEvents();
          CHECK(hdr->stops() == 0.0);
          hdr->intensity_spin()->setValue(1.0);
          QApplication::processEvents();
          dialog->resize(dialog->width(), std::max(dialog->height(), dialog->sizeHint().height()));
          QApplication::processEvents();
          save_capture(*dialog, QStringLiteral("gradient-editor-32bit"));
        }
        checked = true;
      }
      QTimer::singleShot(0, dialog, [dialog] { dialog->accept(); });
    });
    window.findChild<QPushButton*>(QStringLiteral("gradientEditStopsButton"))->click();
    QApplication::processEvents();
    CHECK(checked);
    if (deep) {
      // The edit made the stops custom; the background stop now carries +1.
      CHECK(canvas->gradient_stops().has_value());
      if (canvas->gradient_stops().has_value()) {
        CHECK(canvas->gradient_stops()->back().intensity_stops == 1.0F);
        CHECK(canvas->gradient_stops()->front().intensity_stops == 2.0F);
      }
      canvas->set_gradient_stops(std::nullopt);
    }

    // Foreground to Background across the document.
    const auto from = canvas->widget_position_for_document_point(QPoint(0, 60));
    const auto to = canvas->widget_position_for_document_point(QPoint(239, 60));
    send_mouse(*canvas, QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton);
    send_mouse(*canvas, QEvent::MouseMove, to, Qt::NoButton, Qt::LeftButton);
    send_mouse(*canvas, QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton);
    QApplication::processEvents();
    const auto* layer = active_layer(window);
    CHECK(layer != nullptr);
    if (layer == nullptr) {
      return;
    }
    const auto& pixels = layer->pixels();
    const auto bounds = layer->bounds();
    const auto start = patchy::load_pixel(pixels.format(), pixels.pixel(2 - bounds.x, 60 - bounds.y));
    if (deep) {
      CHECK(start[0] / 255.0F > 3.5F);  // 4.0 red at the foreground end
      canvas->set_tool(patchy::ui::CanvasTool::Eyedropper);
      const auto pick = canvas->widget_position_for_document_point(QPoint(1, 60));
      send_mouse(*canvas, QEvent::MouseButtonPress, pick, Qt::LeftButton, Qt::LeftButton);
      send_mouse(*canvas, QEvent::MouseButtonRelease, pick, Qt::LeftButton, Qt::NoButton);
      QApplication::processEvents();
      CHECK(canvas->paint_intensity().foreground > 1.8);
      CHECK(panel->hdr_controls()->stops() > 1.8);
      save_capture(window, QStringLiteral("gradient-32bit-eyedropper"));
    } else {
      CHECK(start[0] > 250.0F && start[0] <= 255.0F);  // 8 bits: the base color, as before
    }
  }
}

}  // namespace

std::vector<patchy::test::TestCase> hdr_color_choice_tests() {
  return {
      {"ui_hdr_color_choice_captures", ui_hdr_color_choice_captures},
      {"ui_hdr_color_intensity_paints_and_follows", ui_hdr_color_intensity_paints_and_follows},
      {"ui_hdr_eyedropper_keeps_32_bit_values", ui_hdr_eyedropper_keeps_32_bit_values},
      {"ui_hdr_gradient_stops_carry_intensity", ui_hdr_gradient_stops_carry_intensity},
  };
}
