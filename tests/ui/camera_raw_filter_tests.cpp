// Filter > Camera Raw Filter (GitHub issue 392; docs/camera-raw.md "Camera Raw Filter").

#include "core/photo_develop.hpp"
#include "core/pixel_depth.hpp"
#include "local_psd_fixtures.hpp"
#include "scripting_test_support.hpp"
#include "test_harness.hpp"
#include "ui/edit_conversions.hpp"
#include "ui/filter_workflows.hpp"
#include "ui/main_window.hpp"
#include "ui/qt_paths.hpp"
#include "ui/zoomable_image_preview.hpp"
#include "ui_test_access.hpp"
#include "ui_test_groups.hpp"
#include "ui_test_support.hpp"

#include <QDialog>
#include <QKeySequence>
#include <QLabel>
#include <QPoint>
#include <QPushButton>
#include <QScopeGuard>
#include <QSlider>
#include <QTimer>

#include <array>
#include <cstring>
#include <functional>
#include <memory>
#include <optional>

namespace {

using namespace patchy::test::ui;
using patchy::test::ui::scripting_support::backlog_contains;
using patchy::test::ui::scripting_support::run_script;

void drive_dialog(const std::shared_ptr<std::function<bool()>>& step, int attempts = 2400) {
  QTimer::singleShot(25, [step, attempts] {
    if (step == nullptr || !static_cast<bool>(*step) || (*step)()) return;
    if (attempts > 0) drive_dialog(step, attempts - 1);
  });
}

bool same_pixels(const patchy::PixelBuffer& a, const patchy::PixelBuffer& b) {
  if (a.width() != b.width() || a.height() != b.height() || a.format() != b.format()) return false;
  for (std::int32_t y = 0; y < a.height(); ++y) {
    if (std::memcmp(a.row(y).data(), b.row(y).data(), a.row(y).size()) != 0) return false;
  }
  return true;
}

const patchy::Layer* active_layer(patchy::ui::MainWindow& window) {
  const auto& document = std::as_const(patchy::ui::MainWindowTestAccess::document(window));
  const auto id = document.active_layer_id();
  return id.has_value() ? document.find_layer(*id) : nullptr;
}

// The menu command on a photo: the dialog's refined preview, OK applies exactly the
// shared pipeline as one undo step, Undo restores the layer, Cancel changes nothing.
void ui_camera_raw_filter_dialog_applies_one_undo_step() {
  patchy::ui::MainWindow window;
  show_window(window);
  const auto photo = patchy::test::committed_format_fixture_path("readme", "havana_vintage_car_cc0.jpg");
  patchy::ui::MainWindowTestAccess::open_document_path(window, patchy::ui::to_qstring(photo));

  auto* action = require_action(window, "filterCameraRawFilterAction");
  CHECK(action->property("patchy.channelViewBlocked").toBool());
  const auto* command = window.hotkey_registry().find_command(QStringLiteral("filter.camera_raw_filter"));
  CHECK(command != nullptr && command->action == action);
  CHECK(command != nullptr &&
        command->default_shortcuts == QList<QKeySequence>{QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_A)});

  const auto* layer = active_layer(window);
  CHECK(layer != nullptr);
  if (layer == nullptr) return;
  const auto original = std::as_const(*layer).pixels();
  const auto bounds = layer->bounds();
  save_image_artifact("ui_camera_raw_filter_before", patchy::ui::display_qimage_from_pixel_buffer(original));
  const auto undo_before = patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);

  patchy::PhotoDevelopParams expected;
  expected.temperature = 18.0;
  expected.exposure_ev = 0.35;
  expected.contrast = 25.0;
  expected.shadows = 30.0;
  expected.vibrance = 40.0;
  auto stage = std::make_shared<int>(0);
  auto step = std::make_shared<std::function<bool()>>();
  *step = [stage, expected] {
    auto* dialog = find_top_level_dialog(QStringLiteral("cameraRawFilterDialog"));
    if (dialog == nullptr) return false;
    const auto slider = [dialog](const char* name) {
      auto* found = dialog->findChild<QSlider*>(QLatin1String(name));
      CHECK(found != nullptr);
      return found;
    };
    // ZoomableImagePreview has no Q_OBJECT macro, so findChild cannot cast to it.
    auto* preview = static_cast<patchy::ui::ZoomableImagePreview*>(
        dialog->findChild<QWidget*>(QStringLiteral("cameraRawFilterPreview")));
    if (preview == nullptr || preview->image().isNull() ||
        !dialog->property("cameraRawFilterPreviewAccurate").toBool()) {
      return false;
    }
    if (*stage == 0) {
      // Raw-only controls are not part of the filter.
      CHECK(dialog->findChild<QWidget*>(QStringLiteral("rawDemosaicCombo")) == nullptr);
      CHECK(slider("cameraRawFilterTemperatureSlider")->value() == 0);
      // Every group's sliders share one left and right edge; Auto is a text-sized button
      // ending where the sliders end.
      const auto left = [dialog](QWidget* widget) { return widget->mapTo(dialog, QPoint(0, 0)).x(); };
      const auto right = [dialog](QWidget* widget) { return widget->mapTo(dialog, QPoint(widget->width(), 0)).x(); };
      auto* temperature = slider("cameraRawFilterTemperatureSlider");
      for (const char* name : {"cameraRawFilterExposureSlider", "cameraRawFilterShadowsSlider",
                               "cameraRawFilterVibranceSlider"}) {
        CHECK(left(slider(name)) == left(temperature));
        CHECK(right(slider(name)) == right(temperature));
      }
      auto* auto_button = dialog->findChild<QPushButton*>(QStringLiteral("cameraRawFilterAutoWhiteBalanceButton"));
      CHECK(auto_button != nullptr);
      if (auto_button != nullptr) {
        CHECK(auto_button->width() < temperature->width() / 2);
        CHECK(right(auto_button) == right(temperature));
      }
      slider("cameraRawFilterTemperatureSlider")->setValue(static_cast<int>(expected.temperature));
      slider("cameraRawFilterExposureSlider")->setValue(static_cast<int>(expected.exposure_ev * 100.0));
      slider("cameraRawFilterContrastSlider")->setValue(static_cast<int>(expected.contrast));
      slider("cameraRawFilterShadowsSlider")->setValue(static_cast<int>(expected.shadows));
      slider("cameraRawFilterVibranceSlider")->setValue(static_cast<int>(expected.vibrance));
      CHECK(!dialog->property("cameraRawFilterPreviewAccurate").toBool());
      *stage = 1;
      return false;
    }
    if (*stage == 1) {
      save_widget_artifact("ui_camera_raw_filter_dialog", *dialog);
      *stage = 2;
      auto* ok = dialog->findChild<QPushButton*>(QStringLiteral("cameraRawFilterOkButton"));
      CHECK(ok != nullptr);
      if (ok != nullptr) ok->click();
      return true;
    }
    return true;
  };
  drive_dialog(step);
  action->trigger();
  CHECK(*stage == 2);

  layer = active_layer(window);
  CHECK(layer != nullptr);
  if (layer == nullptr) return;
  auto reference = original;
  patchy::ui::apply_camera_raw_filter_to_pixels(reference, bounds, {}, expected);
  CHECK(!same_pixels(reference, original));
  CHECK(same_pixels(std::as_const(*layer).pixels(), reference));
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == undo_before + 1);
  save_image_artifact("ui_camera_raw_filter_after",
                      patchy::ui::display_qimage_from_pixel_buffer(std::as_const(*layer).pixels()));

  patchy::ui::MainWindowTestAccess::undo(window);
  layer = active_layer(window);
  CHECK(layer != nullptr && same_pixels(std::as_const(*layer).pixels(), original));

  // Cancel leaves the layer and the history alone.
  const auto undo_after = patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);
  auto cancelled = std::make_shared<bool>(false);
  auto cancel_step = std::make_shared<std::function<bool()>>();
  *cancel_step = [cancelled] {
    auto* dialog = find_top_level_dialog(QStringLiteral("cameraRawFilterDialog"));
    if (dialog == nullptr) return false;
    if (auto* exposure = dialog->findChild<QSlider*>(QStringLiteral("cameraRawFilterExposureSlider")))
      exposure->setValue(200);
    *cancelled = true;
    QTimer::singleShot(0, dialog, [dialog] { dialog->reject(); });
    return true;
  };
  drive_dialog(cancel_step);
  action->trigger();
  CHECK(*cancelled);
  layer = active_layer(window);
  CHECK(layer != nullptr && same_pixels(std::as_const(*layer).pixels(), original));
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == undo_after);
}

// layer.applyCameraRawFilter on 16 and 32-bit layers: the selection limits it, the result is
// the shared pipeline at the layer's depth, and bad settings throw.
void ui_camera_raw_filter_script_and_selection() {
  patchy::set_deep_editing_override(true);
  const auto restore = qScopeGuard([] { patchy::set_deep_editing_override(std::nullopt); });
  patchy::ui::MainWindow window;
  show_window(window);
  CHECK(run_script(window, QStringLiteral(R"JS(
    var doc = app.newDocument(64, 48);
    doc.activeLayer.fill('#806040');
    doc.convertBitDepth(16);
  )JS")));
  const auto* layer = active_layer(window);
  CHECK(layer != nullptr);
  if (layer == nullptr) return;
  const auto original16 = std::as_const(*layer).pixels();
  CHECK(original16.format().bit_depth == patchy::BitDepth::UInt16);

  CHECK(run_script(window, QStringLiteral(R"JS(
    var doc = app.activeDocument;
    var layer = doc.activeLayer;
    doc.selection.selectRect(0, 0, 32, 48);
    layer.applyCameraRawFilter({exposure: 1, saturation: 20});
    doc.selection.deselect();
    function throws(f) { try { f(); } catch (e) { return true; } return false; }
    console.log('crf-throws=' + throws(function () { layer.applyCameraRawFilter({brightness: 2}); }) + ',' +
                throws(function () { layer.applyCameraRawFilter({exposure: 'a lot'}); }));
  )JS")));
  CHECK(backlog_contains(window, QStringLiteral("crf-throws=true,true")));
  layer = active_layer(window);
  if (layer == nullptr) return;
  const auto& filtered16 = std::as_const(*layer).pixels();
  patchy::PhotoDevelopParams params;
  params.exposure_ev = 1.0;
  params.saturation = 20.0;
  auto expected16 = original16;
  patchy::PhotoDeveloper(params).apply(expected16);
  const auto pixel_bytes = patchy::bytes_per_pixel(filtered16.format());
  CHECK(std::memcmp(filtered16.pixel(10, 20), expected16.pixel(10, 20), pixel_bytes) == 0);
  CHECK(std::memcmp(filtered16.pixel(50, 20), original16.pixel(50, 20), pixel_bytes) == 0);
  CHECK(std::memcmp(filtered16.pixel(10, 20), original16.pixel(10, 20), pixel_bytes) != 0);

  // 32 bits: exposure is an exact multiply of linear light, across the whole layer.
  CHECK(run_script(window, QStringLiteral("app.activeDocument.convertBitDepth(32);")));
  layer = active_layer(window);
  if (layer == nullptr) return;
  const auto original32 = std::as_const(*layer).pixels();
  CHECK(original32.format().bit_depth == patchy::BitDepth::Float32);
  CHECK(run_script(window, QStringLiteral("app.activeDocument.activeLayer.applyCameraRawFilter({exposure: 1});")));
  layer = active_layer(window);
  if (layer == nullptr) return;
  std::array<float, 3> before{};
  std::array<float, 3> after{};
  std::memcpy(before.data(), original32.pixel(50, 20), sizeof(before));
  std::memcpy(after.data(), std::as_const(*layer).pixels().pixel(50, 20), sizeof(after));
  CHECK(after[0] == before[0] * 2.0F && after[1] == before[1] * 2.0F && after[2] == before[2] * 2.0F);
}

}  // namespace

std::vector<patchy::test::TestCase> camera_raw_filter_tests() {
  return {
      {"ui_camera_raw_filter_dialog_applies_one_undo_step", ui_camera_raw_filter_dialog_applies_one_undo_step},
      {"ui_camera_raw_filter_script_and_selection", ui_camera_raw_filter_script_and_selection},
  };
}
