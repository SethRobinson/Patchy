#include "ui/pattern_library.hpp"
#include "ui/brush_tip_library.hpp"
#include "ui/main_window.hpp"
#include "ui/app_settings.hpp"
#include "ui/zoomable_image_preview.hpp"
#include "ui/raw_develop_dialog.hpp"
#include "filters/auto_levels_math.hpp"
#include "ui/raw_develop_histogram.hpp"
#include "ui/raw_develop_settings.hpp"
#include "ui/script_engine.hpp"
#include "ui/qt_paths.hpp"
#include "unicode_path_names.hpp"
#include <QJsonDocument>
#include <QJsonArray>
#include "synthetic_dng.hpp"
#include "test_harness.hpp"
#include "local_psd_fixtures.hpp"
#include <QApplication>
#include <QByteArray>
#include <QCheckBox>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDialog>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QMenu>
#include <QMimeData>
#include <QMessageBox>
#include <QIODevice>
#include <QPushButton>
#include <QSettings>
#include <QSlider>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>
#include <QUrl>
#include <QVariant>
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
#include "ui_test_access.hpp"
#include "ui_test_groups.hpp"
#include "ui_test_support.hpp"

namespace {

using namespace patchy::test::ui;

void ui_flat_save_of_layered_document_warns_and_saves_copy() {
  patchy::Document document(24, 18, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Base",
                           solid_pixels(24, 18, patchy::PixelFormat::rgb8(), QColor(60, 120, 180)));
  document.add_pixel_layer("Top",
                           solid_pixels(24, 18, patchy::PixelFormat::rgb8(), QColor(220, 30, 30)));
  patchy::ui::MainWindow window;
  window.add_document_session(std::move(document), QStringLiteral("Flatten Copy Warning"));
  show_window(window);
  require_action(window, "layerNewAction")->trigger();
  QApplication::processEvents();
  CHECK(patchy::ui::MainWindowTestAccess::active_session_is_modified(window));
  QTemporaryDir temp;
  CHECK(temp.isValid());
  const auto jpg_path = temp.filePath(QStringLiteral("layered.jpg"));
  patchy::ui::ImageSaveOptions options;

  bool cancel_prompt_seen = false;
  QTimer::singleShot(0, [&cancel_prompt_seen] {
    auto* box =
        qobject_cast<QMessageBox*>(find_top_level_dialog(QStringLiteral("flattenLayersMessageBox")));
    CHECK(box != nullptr);
    cancel_prompt_seen = true;
    box->button(QMessageBox::Cancel)->click();
  });
  CHECK(!patchy::ui::MainWindowTestAccess::save_document_to_path(window, jpg_path, options));
  CHECK(cancel_prompt_seen);
  CHECK(!QFileInfo::exists(jpg_path));

  bool save_prompt_seen = false;
  QTimer::singleShot(0, [&save_prompt_seen] {
    auto* box =
        qobject_cast<QMessageBox*>(find_top_level_dialog(QStringLiteral("flattenLayersMessageBox")));
    CHECK(box != nullptr);
    save_prompt_seen = true;
    box->button(QMessageBox::Save)->click();
  });
  CHECK(patchy::ui::MainWindowTestAccess::save_document_to_path(window, jpg_path, options));
  CHECK(save_prompt_seen);
  CHECK(QFileInfo::exists(jpg_path));

  // Photoshop's save-a-copy semantics: only the flat copy went to disk; the layered
  // document is still the open, modified, untitled document.
  CHECK(patchy::ui::MainWindowTestAccess::active_session_is_modified(window));
  CHECK(patchy::ui::MainWindowTestAccess::active_session_path(window).isEmpty());

  // Saving as PSD is a real save: no flatten prompt, adopts the path, clears modified.
  const auto psd_path = temp.filePath(QStringLiteral("layered.psd"));
  CHECK(patchy::ui::MainWindowTestAccess::save_document_to_path(window, psd_path, options));
  CHECK(QFileInfo::exists(psd_path));
  CHECK(!patchy::ui::MainWindowTestAccess::active_session_is_modified(window));
  CHECK(patchy::ui::MainWindowTestAccess::active_session_path(window) == psd_path);
}

// ---- Camera raw develop dialog ----

QString write_raw_dng_fixture(const QString& file_name) {
  ensure_artifact_dir();
  const auto path = QFileInfo(QDir(QStringLiteral("test-artifacts")).filePath(file_name)).absoluteFilePath();
  QFile::remove(patchy::ui::raw_develop_settings_path(path));
  const auto bytes = patchy::test::synthetic_bayer_dng(128, 96);
  QFile file(path);
  CHECK(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
  CHECK(file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<qsizetype>(bytes.size())) ==
        static_cast<qsizetype>(bytes.size()));
  return path;
}

// Isolate legacy values for older tests; per-photo settings must never read them.
// Snapshots and removes them, restoring the original state afterwards. The
// imports/showRawDevelopDialog preference has a different prefix and is unaffected.
class RawDevelopSettingsSanitizer {
public:
  RawDevelopSettingsSanitizer() : settings_(patchy::ui::app_settings()) {
    const auto keys = settings_.allKeys();
    for (const auto& key : keys) {
      if (key.startsWith(QStringLiteral("imports/rawDevelop"))) {
        saved_.insert(key, settings_.value(key));
        settings_.remove(key);
      }
    }
    settings_.sync();
  }

  ~RawDevelopSettingsSanitizer() {
    const auto keys = settings_.allKeys();
    for (const auto& key : keys) {
      if (key.startsWith(QStringLiteral("imports/rawDevelop")) && !saved_.contains(key)) {
        settings_.remove(key);
      }
    }
    for (auto it = saved_.constBegin(); it != saved_.constEnd(); ++it) {
      settings_.setValue(it.key(), it.value());
    }
    settings_.sync();
  }

private:
  QSettings settings_;
  QMap<QString, QVariant> saved_;
};

// Repeatedly runs `step` on a short timer while open_document_path blocks in the raw
// develop dialog's exec() loop; `step` returns true when its work is done.
void drive_raw_develop_dialog(const std::shared_ptr<std::function<bool()>>& step, int attempts = 2400) {
  QTimer::singleShot(25, [step, attempts] {
    if (step == nullptr || !static_cast<bool>(*step)) {
      return;
    }
    if ((*step)()) {
      return;
    }
    if (attempts > 0) {
      drive_raw_develop_dialog(step, attempts - 1);
    }
  });
}

QByteArray raw_test_bytes(const QString& path) {
  QFile file(path);
  CHECK(file.open(QIODevice::ReadOnly));
  return file.readAll();
}

void raw_test_write(const QString& path, const QByteArray& bytes) {
  QFile file(path);
  CHECK(file.open(QIODevice::WriteOnly));
  CHECK(file.write(bytes) == bytes.size());
}

// Hang guard for the dialog drivers below, not a performance bound: the develop
// dialog must eventually close by itself or through the driver. Two
// full-resolution 26 MP ARW develops finish well inside this on an idle machine;
// the old 30 s limit was close enough to the honest cost that a concurrent build
// pushed it over.
constexpr qint64 kRawDialogDeadlineMs = 120000;

std::optional<patchy::ui::RawDevelopOutcome> raw_test_dialog(
    const QString& path, const std::function<void(QDialog&)>& step) {
  std::exception_ptr error;
  QElapsedTimer elapsed;
  elapsed.start();
  QTimer timer;
  QObject::connect(&timer, &QTimer::timeout, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("rawDevelopDialog"));
    if (!dialog) return;
    try {
      CHECK(elapsed.elapsed() < kRawDialogDeadlineMs);
      step(*dialog);
    } catch (...) {
      error = std::current_exception();
      dialog->reject();
      timer.stop();
    }
  });
  timer.start(15);
  auto result = patchy::ui::run_raw_develop_dialog(nullptr, path);
  timer.stop();
  if (error) std::rethrow_exception(error);
  return result;
}

void ui_raw_sidecar_round_trips_unicode_and_preserves_unknown_fields() {
  using namespace patchy::ui;
  for (const auto stem : patchy::test::kUnicodePathStems) {
    const auto utf8 = patchy::test::utf8_string(stem);
    const auto path = write_raw_dng_fixture(QString::fromUtf8(utf8.data(), static_cast<qsizetype>(utf8.size())) + QStringLiteral(".ARW"));
    const auto source_bytes = raw_test_bytes(path);
    auto initial = load_raw_develop_settings(path);
    CHECK(!initial.exists);
    CHECK(save_raw_develop_settings(path, {}, initial).isEmpty());
    CHECK(!QFileInfo::exists(raw_develop_settings_path(path)));
    patchy::raw::DevelopParams params;
    params.white_balance = patchy::raw::WhiteBalanceMode::Custom;
    params.custom_white_balance = {4300.0, 12.0};
    params.exposure_ev = 0.75;
    params.highlight_recovery = patchy::raw::HighlightMode::Rebuild;
    params.auto_brighten = true;
    params.brightness = 1.25;
    params.contrast = 13;
    params.highlights = -24;
    params.shadows = 16;
    params.saturation = 7;
    params.vibrance = 9;
    params.demosaic = patchy::raw::DemosaicAlgorithm::Dht;
    params.noise_reduction = patchy::raw::NoiseReductionMode::Manual;
    params.wavelet_denoise_threshold = 123;
    params.fbdd = patchy::raw::FbddNoiseReduction::Full;
    params.profile = patchy::raw::RenderingProfile::Neutral;
    params.color_denoise_passes = 3;
    params.half_size = true;
    CHECK(save_raw_develop_settings(path, params, initial).isEmpty());
    auto loaded = load_raw_develop_settings(path);
    CHECK(loaded.recognized);
    CHECK(loaded.params == params);
    auto root = loaded.preserved;
    root.insert(QStringLiteral("future"), QStringLiteral("keep"));
    auto fields = root.value(QStringLiteral("parameters")).toObject();
    fields.insert(QStringLiteral("futureParameter"), 17);
    root.insert(QStringLiteral("parameters"), fields);
    raw_test_write(raw_develop_settings_path(path), QJsonDocument(root).toJson());
    loaded = load_raw_develop_settings(path);
    params.exposure_ev = 1.0;
    CHECK(save_raw_develop_settings(path, params, loaded).isEmpty());
    loaded = load_raw_develop_settings(path);
    CHECK(loaded.preserved.value(QStringLiteral("future")).toString() == QStringLiteral("keep"));
    CHECK(loaded.preserved.value(QStringLiteral("parameters")).toObject().value(QStringLiteral("futureParameter")).toInt() == 17);
    CHECK(raw_test_bytes(path) == source_bytes);
    CHECK(save_raw_develop_settings(path, {}, loaded).isEmpty());
    CHECK(!QFileInfo::exists(raw_develop_settings_path(path)));
  }
}

void ui_raw_sidecar_rejects_invalid_and_preserves_failed_writes() {
  using namespace patchy::ui;
  const auto path = write_raw_dng_fixture(QStringLiteral("raw_sidecar_invalid.dng"));
  patchy::raw::DevelopParams params;
  params.exposure_ev = 1.0;
  CHECK(save_raw_develop_settings(path, params, load_raw_develop_settings(path)).isEmpty());
  const auto valid = load_raw_develop_settings(path).preserved;
  const std::array<QJsonValue, 5> invalid_values{QJsonValue(QStringLiteral("1")), QJsonValue(true), QJsonValue(4), QJsonValue(), QJsonValue(QJsonArray{})};
  for (const auto& invalid : invalid_values) {
    auto root = valid;
    auto fields = root.value(QStringLiteral("parameters")).toObject();
    fields.insert(QStringLiteral("exposure"), invalid);
    root.insert(QStringLiteral("parameters"), fields);
    const auto bytes = QJsonDocument(root).toJson();
    raw_test_write(raw_develop_settings_path(path), bytes);
    const auto loaded = load_raw_develop_settings(path);
    CHECK(!loaded.recognized && !loaded.notice.isEmpty());
    CHECK(loaded.params == patchy::raw::DevelopParams{});
    CHECK(!save_raw_develop_settings(path, params, loaded).isEmpty());
    CHECK(raw_test_bytes(raw_develop_settings_path(path)) == bytes);
  }
  for (const auto* key : {"version", "processingVersion"}) {
    auto root = valid;
    root.insert(QString::fromLatin1(key), 999);
    raw_test_write(raw_develop_settings_path(path), QJsonDocument(root).toJson());
    CHECK(!load_raw_develop_settings(path).recognized);
  }
  for (const auto* key : {"profile", "colorDenoisePasses"}) {
    for (const auto& value : {QJsonValue(), QJsonValue(true), QJsonValue(QStringLiteral("unknown")), QJsonValue(2.5), QJsonValue(5)}) {
      auto root = valid;
      auto fields = root.value(QStringLiteral("parameters")).toObject();
      fields.insert(QString::fromLatin1(key), value);
      root.insert(QStringLiteral("parameters"), fields);
      raw_test_write(raw_develop_settings_path(path), QJsonDocument(root).toJson());
      CHECK(!load_raw_develop_settings(path).recognized);
    }
  }
  raw_test_write(raw_develop_settings_path(path), QByteArray("not json"));
  auto loaded = load_raw_develop_settings(path);
  CHECK(save_raw_develop_settings(path, params, loaded, true).isEmpty());
  loaded = load_raw_develop_settings(path);
  raw_test_write(raw_develop_settings_path(path), QByteArray("changed externally"));
  CHECK(!save_raw_develop_settings(path, params, loaded).isEmpty());
  CHECK(raw_test_bytes(raw_develop_settings_path(path)) == QByteArray("changed externally"));
  const auto missing_parent = path + QStringLiteral("/missing/photo.ARW");
  CHECK(!save_raw_develop_settings(missing_parent, params, {}).isEmpty());
  QFile::remove(raw_develop_settings_path(path));
}

void ui_raw_legacy_sidecars_preserve_processing_and_upgrade_explicitly() {
  using namespace patchy::ui;
  using namespace patchy::raw;
  const auto path = write_raw_dng_fixture(QStringLiteral("raw_original_processing.dng"));
  patchy::test::SyntheticDngOptions fixture;
  fixture.iso = 5000;
  const auto bytes = patchy::test::synthetic_bayer_dng(257, 193, fixture);
  raw_test_write(path, QByteArray(reinterpret_cast<const char*>(bytes.data()), static_cast<qsizetype>(bytes.size())));
  DevelopParams params;
  params.exposure_ev = 0.125;
  CHECK(save_raw_develop_settings(path, params, {}).isEmpty());
  auto root = load_raw_develop_settings(path).preserved;
  root.insert(QStringLiteral("processingVersion"), 1);
  auto fields = root.value(QStringLiteral("parameters")).toObject();
  fields.remove(QStringLiteral("profile"));
  fields.remove(QStringLiteral("colorDenoisePasses"));
  fields.insert(QStringLiteral("futureSetting"), 42);
  root.insert(QStringLiteral("parameters"), fields);
  raw_test_write(raw_develop_settings_path(path), QJsonDocument(root).toJson());
  auto loaded = load_raw_develop_settings(path);
  CHECK(loaded.recognized && loaded.notice.isEmpty());
  CHECK(loaded.params.processing_version == 1 && loaded.params.profile == RenderingProfile::Neutral);
  const auto original_bytes = loaded.original_bytes;
  bool opened = false;
  CHECK(raw_test_dialog(path, [&](QDialog& dialog) {
    if (opened || !dialog.property("rawInfoReady").toBool()) return;
    CHECK(dialog.findChild<QComboBox*>(QStringLiteral("rawProfileCombo"))->currentData().toString() == QStringLiteral("neutral"));
    CHECK(dialog.findChild<QLabel*>(QStringLiteral("rawProcessingNote"))->isVisible());
    CHECK(dialog.findChild<QComboBox*>(QStringLiteral("rawColorNoiseCombo"))->currentData().toInt() == 0);
    CHECK(dialog.findChild<QSlider*>(QStringLiteral("rawDenoiseSlider"))->value() == 182);
    opened = true;
    dialog.findChild<QPushButton*>(QStringLiteral("rawOpenButton"))->click();
  }).has_value());
  CHECK(opened);
  CHECK(raw_test_bytes(raw_develop_settings_path(path)) == original_bytes);
  // Ordinary edits preserve the old processing contract, including unknown fields.
  params = loaded.params;
  params.exposure_ev = 0.25;
  CHECK(save_raw_develop_settings(path, params, loaded).isEmpty());
  loaded = load_raw_develop_settings(path);
  CHECK(loaded.params.processing_version == 1);
  CHECK(loaded.preserved.value(QStringLiteral("parameters")).toObject().value(QStringLiteral("futureSetting")).toInt() == 42);
  bool changed = false;
  auto outcome = raw_test_dialog(path, [&](QDialog& dialog) {
    if (changed || !dialog.property("rawPreviewAccurate").toBool()) return;
    auto* profile = dialog.findChild<QComboBox*>(QStringLiteral("rawProfileCombo"));
    profile->setCurrentIndex(profile->findData(QStringLiteral("natural")));
    CHECK(!dialog.findChild<QLabel*>(QStringLiteral("rawProcessingNote"))->isVisible());
    CHECK(dialog.findChild<QComboBox*>(QStringLiteral("rawColorNoiseCombo"))->currentData().toInt() == 2);
    CHECK(dialog.findChild<QSlider*>(QStringLiteral("rawDenoiseSlider"))->value() == 364);
    changed = true;
    dialog.findChild<QPushButton*>(QStringLiteral("rawOpenButton"))->click();
    CHECK(!profile->isEnabled());
  });
  CHECK(changed && outcome.has_value());
  CHECK(outcome->params.processing_version == kProcessingVersion && outcome->params.profile == RenderingProfile::Natural);
  loaded = load_raw_develop_settings(path);
  CHECK(loaded.params == outcome->params);
  const auto expected = read_camera_raw(bytes, loaded.params);
  const auto& actual = std::as_const(outcome->document).layers().front().pixels();
  const auto& reference = expected.document.layers().front().pixels();
  for (int y = 0; y < actual.height(); ++y)
    CHECK(std::equal(actual.row(y).begin(), actual.row(y).end(), reference.row(y).begin()));
  CHECK(raw_test_bytes(path) == QByteArray(reinterpret_cast<const char*>(bytes.data()), static_cast<qsizetype>(bytes.size())));
  // Version 2 Natural settings also remain recognized and byte-identical on Open.
  params = {};
  params.processing_version = 2;
  CHECK(save_raw_develop_settings(path, params, loaded).isEmpty());
  loaded = load_raw_develop_settings(path);
  CHECK(loaded.recognized && loaded.params.processing_version == 2);
  opened = false;
  CHECK(raw_test_dialog(path, [&](QDialog& dialog) {
    if (opened || !dialog.property("rawInfoReady").toBool()) return;
    CHECK(dialog.findChild<QLabel*>(QStringLiteral("rawProcessingNote"))->isVisible());
    opened = true;
    dialog.findChild<QPushButton*>(QStringLiteral("rawOpenButton"))->click();
  }).has_value());
  CHECK(opened);
  CHECK(raw_test_bytes(raw_develop_settings_path(path)) == loaded.original_bytes);
}

void ui_raw_dialog_open_cancel_reset_and_global_isolation() {
  using namespace patchy::ui;
  SettingsValueRestorer global(QStringLiteral("imports/rawDevelopExposure"));
  auto settings = app_settings();
  settings.setValue(QStringLiteral("imports/rawDevelopExposure"), 2.0);
  settings.sync();
  const auto a = write_raw_dng_fixture(QStringLiteral("raw_per_photo_a.dng"));
  const auto b = write_raw_dng_fixture(QStringLiteral("raw_per_photo_b.dng"));
  bool changed = false;
  CHECK(raw_test_dialog(a, [&](QDialog& dialog) {
    if (changed || !dialog.property("rawInfoReady").toBool()) return;
    auto* exposure = dialog.findChild<QSlider*>(QStringLiteral("rawExposureSlider"));
    CHECK(exposure->value() == 0);
    exposure->setValue(125);
    changed = true;
    dialog.findChild<QPushButton*>(QStringLiteral("rawOpenButton"))->click();
  }).has_value());
  CHECK(changed);
  CHECK(load_raw_develop_settings(a).params.exposure_ev == 1.25);
  CHECK(!load_raw_develop_settings(b).exists);
  const auto saved = raw_test_bytes(raw_develop_settings_path(a));
  CHECK(!raw_test_dialog(a, [&](QDialog& dialog) {
    auto* exposure = dialog.findChild<QSlider*>(QStringLiteral("rawExposureSlider"));
    CHECK(exposure->value() == 125);
    exposure->setValue(-100);
    dialog.reject();
  }));
  CHECK(raw_test_bytes(raw_develop_settings_path(a)) == saved);
  CHECK(!raw_test_dialog(a, [&](QDialog& dialog) {
    dialog.findChild<QPushButton*>(QStringLiteral("rawResetButton"))->click();
    dialog.reject();
  }));
  CHECK(raw_test_bytes(raw_develop_settings_path(a)) == saved);
  changed = false;
  CHECK(raw_test_dialog(a, [&](QDialog& dialog) {
    if (changed || !dialog.property("rawInfoReady").toBool()) return;
    dialog.findChild<QPushButton*>(QStringLiteral("rawResetButton"))->click();
    CHECK(dialog.findChild<QSlider*>(QStringLiteral("rawExposureSlider"))->value() == 0);
    changed = true;
    dialog.findChild<QPushButton*>(QStringLiteral("rawOpenButton"))->click();
  }).has_value());
  CHECK(changed);
  CHECK(!load_raw_develop_settings(a).exists);
  CHECK(app_settings().value(QStringLiteral("imports/rawDevelopExposure")).toDouble() == 2.0);
}

void ui_raw_dialog_preview_matches_open_and_script() {
  using namespace patchy::ui;
  const auto path = write_raw_dng_fixture(QStringLiteral("raw_preview_parity.dng"));
  QImage accurate;
  int stage = 0;
  const auto outcome = raw_test_dialog(path, [&](QDialog& dialog) {
    auto* preview = dynamic_cast<ZoomableImagePreview*>(dialog.findChild<QWidget*>(QStringLiteral("rawDevelopPreview")));
    if (stage == 0) {
      dialog.findChild<QSlider*>(QStringLiteral("rawExposureSlider"))->setValue(40);
      dialog.findChild<QSlider*>(QStringLiteral("rawExposureSlider"))->setValue(80);
      dialog.findChild<QSlider*>(QStringLiteral("rawExposureSlider"))->setValue(100);
      stage = 1;
      return;
    }
    if (!dialog.property("rawPreviewAccurate").toBool() || !preview->property("previewScaleReady").toBool()) return;
    accurate = preview->image();
    save_widget_artifact("ui_raw_refined_preview", dialog);
    dialog.findChild<QPushButton*>(QStringLiteral("rawOpenButton"))->click();
  });
  CHECK(outcome.has_value());
  CHECK(!accurate.isNull());
  const auto verify = [&](const patchy::Document& document) {
    CHECK(document.width() == accurate.width());
    CHECK(document.height() == accurate.height());
    const auto& pixels = document.layers().front().pixels();
    for (int y = 0; y < document.height(); ++y)
      for (int x = 0; x < document.width(); ++x) {
        const auto color = accurate.pixel(x, y);
        CHECK(pixels.pixel(x, y)[0] == qRed(color));
        CHECK(pixels.pixel(x, y)[1] == qGreen(color));
        CHECK(pixels.pixel(x, y)[2] == qBlue(color));
      }
  };
  verify(outcome->document);
  const auto sidecar = raw_test_bytes(raw_develop_settings_path(path));
  SettingsValueRestorer pref(QStringLiteral("imports/showRawDevelopDialog"));
  app_settings().setValue(QStringLiteral("imports/showRawDevelopDialog"), false);
  MainWindow window;
  show_window_empty(window);
  MainWindowTestAccess::open_document_path(window, path);
  verify(MainWindowTestAccess::document(window));
  CHECK(raw_test_bytes(raw_develop_settings_path(path)) == sidecar);
  // Reopen must re-read the sidecar, rather than retaining the open document's recipe.
  auto changed_settings = load_raw_develop_settings(path);
  auto changed_params = changed_settings.params;
  changed_params.exposure_ev = 0.5;
  CHECK(save_raw_develop_settings(path, changed_params, changed_settings).isEmpty());
  std::exception_ptr reopen_error;
  bool reopened = false;
  QTimer reopen_timer;
  reopen_timer.setSingleShot(true);
  QObject::connect(&reopen_timer, &QTimer::timeout, &reopen_timer, [&] {
    for (auto* widget : QApplication::topLevelWidgets()) {
      auto* menu = qobject_cast<QMenu*>(widget);
      if (!menu || menu->objectName() != QStringLiteral("documentTabContextMenu")) continue;
      try {
        for (auto* action : menu->actions()) {
          if (action->objectName() == QStringLiteral("documentTabReopenAction")) {
            CHECK(action->isEnabled());
            action->trigger();
            reopened = true;
          }
        }
      } catch (...) { reopen_error = std::current_exception(); }
      menu->close();
    }
  });
  reopen_timer.start(0);
  auto* tabs = window.findChild<QTabWidget*>(QStringLiteral("documentTabs"));
  CHECK(tabs != nullptr);
  auto* tab_bar = tabs->findChild<QTabBar*>();
  CHECK(tab_bar != nullptr);
  const auto context_point = tab_bar->tabRect(tab_bar->currentIndex()).center();
  QContextMenuEvent context_event(QContextMenuEvent::Mouse, context_point, tab_bar->mapToGlobal(context_point));
  QApplication::sendEvent(tab_bar, &context_event);
  if (reopen_error) std::rethrow_exception(reopen_error);
  CHECK(reopened);
  CHECK(std::as_const(MainWindowTestAccess::document(window)).layers().front().pixels().pixel(60, 40)[1] < qGreen(accurate.pixel(60, 40)));
  raw_test_write(raw_develop_settings_path(path), sidecar);
  app_settings().setValue(QStringLiteral("imports/showRawDevelopDialog"), true);
  for (const bool unattended : {false, true}) {
    MainWindow script_window;
    show_window_empty(script_window);
    auto& host = script_window.script_engine_host();
    // JSON serialization keeps quotes, backslashes and Unicode literal in JS.
    const auto quoted_array = QJsonDocument(QJsonArray{path}).toJson(QJsonDocument::Compact);
    const auto script = QStringLiteral("app.open(%1[0]);").arg(QString::fromUtf8(quoted_array));
    ScriptEngineHost::RunOptions options;
    options.name = QStringLiteral("raw-sidecar-open");
    options.unattended = unattended;
    bool saw_dialog = false;
    QTimer unexpected_dialog;
    QObject::connect(&unexpected_dialog, &QTimer::timeout, [&] {
      if (auto* dialog = find_top_level_dialog(QStringLiteral("rawDevelopDialog"))) {
        saw_dialog = true;
        dialog->reject();
      }
    });
    unexpected_dialog.start(10);
    CHECK(host.run_source(script, std::move(options)));
    CHECK(process_events_until([&] { return !host.run_active(); }, 15000));
    CHECK(!host.last_run_had_error());
    CHECK(!saw_dialog);
    verify(MainWindowTestAccess::document(script_window));
    CHECK(raw_test_bytes(raw_develop_settings_path(path)) == sidecar);
  }
}

void ui_raw_quick_edits_preempt_refinement_and_initial_open_waits() {
  using namespace patchy::ui;
  const auto path = write_raw_dng_fixture(QStringLiteral("raw_responsive.dng"));
  patchy::test::SyntheticDngOptions fixture;
  fixture.iso = 5000;
  fixture.horizontal_ramp = true;
  const auto bytes = patchy::test::synthetic_bayer_dng(2601, 1703, fixture);
  raw_test_write(path, QByteArray(reinterpret_cast<const char*>(bytes.data()), static_cast<qsizetype>(bytes.size())));
  int stage = 0;
  qint64 old_image = 0;
  bool quick_update = false;
  QElapsedTimer response;
  const auto outcome = raw_test_dialog(path, [&](QDialog& dialog) {
    auto* preview = dynamic_cast<ZoomableImagePreview*>(dialog.findChild<QWidget*>(QStringLiteral("rawDevelopPreview")));
    if (stage == 0) {
      const int percent = dialog.property("rawProgressPercent").toInt();
      if (!dialog.property("rawProgressAccurate").toBool() || percent < 20 || percent >= 100) return;
      auto* label = dialog.findChild<QLabel*>(QStringLiteral("rawDevelopStatus"));
      CHECK(label && label->text().contains(QLatin1Char('%')));
      old_image = preview->image().cacheKey();
      auto* contrast = dialog.findChild<QSlider*>(QStringLiteral("rawContrastSlider"));
      contrast->setSliderDown(true);
      contrast->setValue(25);
      contrast->setValue(35);
      contrast->setSliderDown(false); // release must still deliver the quick frame
      response.start();
      stage = 1;
      return;
    }
    if (stage == 1) {
      if (preview->image().cacheKey() == old_image) return;
      CHECK(!dialog.property("rawPreviewAccurate").toBool());
      CHECK(std::max(preview->image().width(), preview->image().height()) <= 1280);
      std::cout << "[INFO] RAW quick edit while refining: " << response.elapsed() << " ms\n";
      quick_update = true;
      stage = 2;
    }
    if (stage == 2 && dialog.property("rawPreviewAccurate").toBool()) {
      old_image = preview->image().cacheKey();
      dialog.findChild<QSlider*>(QStringLiteral("rawContrastSlider"))->setValue(0);
      stage = 3;
    } else if (stage == 3 && preview->image().cacheKey() != old_image) {
      CHECK(!dialog.property("rawPreviewAccurate").toBool());
      dialog.findChild<QSlider*>(QStringLiteral("rawContrastSlider"))->setValue(35);
      stage = 4;
    } else if (stage == 4 && dialog.property("rawPreviewAccurate").toBool()) {
      CHECK(dialog.property("rawProgressPercent").toInt() == 100);
      dialog.findChild<QPushButton*>(QStringLiteral("rawOpenButton"))->click();
      CHECK(dialog.result() == QDialog::Accepted); // Cached accurate pixels open immediately.
      CHECK(dialog.property("rawProgressPercent").toInt() == 100);
    }
  });
  CHECK(quick_update && outcome.has_value() && outcome->params.contrast == 35);
  const auto expected = patchy::raw::read_camera_raw(bytes, outcome->params);
  const auto& actual_pixels = std::as_const(outcome->document).layers().front().pixels();
  const auto& expected_pixels = expected.document.layers().front().pixels();
  for (int y = 0; y < actual_pixels.height(); ++y)
    CHECK(std::equal(actual_pixels.row(y).begin(), actual_pixels.row(y).end(), expected_pixels.row(y).begin()));
  QFile::remove(raw_develop_settings_path(path));
  bool clicked = false;
  bool saw_open_progress = false;
  const auto initial = raw_test_dialog(path, [&](QDialog& dialog) {
    if (!clicked) {
      clicked = true;
      dialog.findChild<QSlider*>(QStringLiteral("rawSaturationSlider"))->setValue(10);
      dialog.findChild<QPushButton*>(QStringLiteral("rawOpenButton"))->click();
    }
    auto* label = dialog.findChild<QLabel*>(QStringLiteral("rawDevelopStatus"));
    CHECK(label != nullptr);
    const int percent = dialog.property("rawProgressPercent").toInt();
    if (percent == 100) {
      CHECK(dialog.property("rawPreviewAccurate").toBool());
      CHECK(label->text().isEmpty());
      return; // Processing is complete; saving/closing can still deliver a timer.
    }
    CHECK(percent >= 0 && percent < 100);
    const bool accurate = dialog.property("rawProgressAccurate").toBool();
    const auto text = accurate ? QObject::tr("Developing full resolution... %1%")
                               : QObject::tr("Preparing RAW... %1%");
    CHECK(label->text() == text.arg(percent));
    saw_open_progress = saw_open_progress || (accurate && percent > 0);
  });
  CHECK(clicked && saw_open_progress && initial.has_value() && initial->params.saturation == 10);
  CHECK(initial->document.width() == 2601 && initial->document.height() == 1703);
}

void ui_raw_open_progress_continues_matching_refinement() {
  using namespace patchy::ui;
  const auto path = write_raw_dng_fixture(QStringLiteral("raw_open_progress.dng"));
  patchy::test::SyntheticDngOptions fixture;
  fixture.iso = 5000;
  fixture.horizontal_ramp = true;
  const auto bytes = patchy::test::synthetic_bayer_dng(2601, 1703, fixture);
  raw_test_write(path, QByteArray(reinterpret_cast<const char*>(bytes.data()), static_cast<qsizetype>(bytes.size())));
  for (const bool half_size : {false, true}) {
    QFile::remove(raw_develop_settings_path(path));
    bool configured = false;
    bool clicked_open = false;
    bool advanced = false;
    int previous_percent = 0;
    const auto result = raw_test_dialog(path, [&](QDialog& dialog) {
      if (!configured) {
        configured = true;
        dialog.findChild<QCheckBox*>(QStringLiteral("rawHalfSizeCheck"))->setChecked(half_size);
        return;
      }
      const bool accurate = dialog.property("rawProgressAccurate").toBool();
      const int percent = dialog.property("rawProgressPercent").toInt();
      auto* label = dialog.findChild<QLabel*>(QStringLiteral("rawDevelopStatus"));
      auto* button = dialog.findChild<QPushButton*>(QStringLiteral("rawOpenButton"));
      CHECK(label != nullptr && button != nullptr);
      if (!clicked_open) {
        if (!accurate || percent < 20 || percent >= 80) return;
        previous_percent = percent;
        clicked_open = true;
        button->click();
        // Open joins the active accurate decode, including its last checkpoint.
        CHECK(dialog.property("rawProgressPercent").toInt() == percent);
        save_widget_artifact(half_size ? "ui_raw_open_half_progress" : "ui_raw_open_full_progress", dialog);
      } else {
        CHECK(accurate);
        CHECK(percent >= previous_percent && percent <= 100);
        if (percent == 100) {
          CHECK(dialog.property("rawPreviewAccurate").toBool());
          CHECK(label->text().isEmpty());
          return; // A completed image is already ready for the import.
        }
        advanced = advanced || percent > previous_percent;
        previous_percent = percent;
      }
      CHECK(!button->isEnabled());
      const auto text = half_size ? QObject::tr("Developing half size... %1%")
                                  : QObject::tr("Developing full resolution... %1%");
      CHECK(label->text() == text.arg(percent));
    });
    CHECK(clicked_open && advanced && result.has_value());
    CHECK(result->document.width() == (half_size ? 1301 : 2601));
    CHECK(result->document.height() == (half_size ? 852 : 1703));
  }
}

void ui_raw_dialog_auto_controls_and_open_during_refinement() {
  using namespace patchy::ui;
  const auto path = write_raw_dng_fixture(QStringLiteral("raw_auto_controls.dng"));
  patchy::test::SyntheticDngOptions fixture;
  fixture.iso = 5000;
  fixture.red_value = 18000;
  fixture.blue_value = 7000;
  const auto bytes = patchy::test::synthetic_bayer_dng(513, 385, fixture);
  raw_test_write(path, QByteArray(reinterpret_cast<const char*>(bytes.data()), static_cast<qsizetype>(bytes.size())));
  int stage = 0;
  const auto result = raw_test_dialog(path, [&](QDialog& dialog) {
    auto* noise = dialog.findChild<QComboBox*>(QStringLiteral("rawNoiseReductionCombo"));
    auto* denoise = dialog.findChild<QSlider*>(QStringLiteral("rawDenoiseSlider"));
    auto* fbdd = dialog.findChild<QComboBox*>(QStringLiteral("rawFbddCombo"));
    auto* color_noise = dialog.findChild<QComboBox*>(QStringLiteral("rawColorNoiseCombo"));
    auto* wb = dialog.findChild<QComboBox*>(QStringLiteral("rawWhiteBalanceCombo"));
    auto* temperature = dialog.findChild<QSlider*>(QStringLiteral("rawTemperatureSlider"));
    auto* label = dialog.findChild<QLabel*>(QStringLiteral("rawTemperatureSliderValue"));
    if (stage == 0) {
      if (!dialog.property("rawPreviewAccurate").toBool()) return;
      CHECK(denoise->value() == 364 && !denoise->isEnabled());
      CHECK(color_noise->currentData().toInt() == 2 && !color_noise->isEnabled());
      CHECK(fbdd->currentData().toString() == QStringLiteral("full") && !fbdd->isEnabled());
      noise->setCurrentIndex(noise->findData(QStringLiteral("manual")));
      CHECK(denoise->value() == 364 && denoise->isEnabled());
      CHECK(color_noise->currentData().toInt() == 2 && color_noise->isEnabled());
      CHECK(fbdd->currentData().toString() == QStringLiteral("full"));
      noise->setCurrentIndex(noise->findData(QStringLiteral("off")));
      CHECK(denoise->value() == 0 && !denoise->isEnabled());
      CHECK(color_noise->currentData().toInt() == 0 && !color_noise->isEnabled());
      wb->setCurrentIndex(wb->findData(QStringLiteral("auto")));
      CHECK(label->text() == QObject::tr("Calculating..."));
      stage = 1;
    } else if (stage == 1) {
      if (!dialog.property("rawPreviewAccurate").toBool()) return;
      CHECK(label->text() != QObject::tr("Calculating..."));
      const auto effective_temperature = temperature->value();
      wb->setCurrentIndex(wb->findData(QStringLiteral("custom")));
      CHECK(temperature->value() == effective_temperature);
      dialog.findChild<QSlider*>(QStringLiteral("rawExposureSlider"))->setValue(75);
      CHECK(!dialog.property("rawPreviewAccurate").toBool());
      stage = 2;
      dialog.findChild<QPushButton*>(QStringLiteral("rawOpenButton"))->click();
    }
  });
  CHECK(stage == 2 && result.has_value());
  patchy::raw::DevelopSession session(bytes);
  const auto accurate = session.develop_document(result->params);
  CHECK(result->document.width() == 513);
  const auto& actual = std::as_const(result->document).layers().front().pixels();
  const auto& expected = accurate.document.layers().front().pixels();
  for (int y = 0; y < actual.height(); ++y)
    CHECK(std::equal(actual.row(y).begin(), actual.row(y).end(), expected.row(y).begin()));
}

void ui_raw_dialog_failed_saves_and_untouched_precision() {
  using namespace patchy::ui;
  const auto path = write_raw_dng_fixture(QStringLiteral("raw_save_failure.dng"));
  const auto sidecar_path = raw_develop_settings_path(path);
  int stage = 0;
  std::exception_ptr prompt_error;
  QTimer prompt_timer;
  QObject::connect(&prompt_timer, &QTimer::timeout, [&] {
    auto* box = qobject_cast<QMessageBox*>(find_top_level_dialog(QStringLiteral("rawSettingsSaveMessageBox")));
    if (!box) return;
    try {
      CHECK(stage == 1);
      stage = 2;
      box->findChild<QPushButton*>(QStringLiteral("rawOpenWithoutSavingButton"))->click();
    } catch (...) {
      prompt_error = std::current_exception();
      box->reject();
    }
  });
  prompt_timer.start(10);
  const auto outcome = raw_test_dialog(path, [&](QDialog& dialog) {
    if (stage != 0 || !dialog.property("rawInfoReady").toBool()) return;
    dialog.findChild<QSlider*>(QStringLiteral("rawExposureSlider"))->setValue(50);
    // An external directory at the sidecar path deterministically prevents saving.
    CHECK(QDir().mkdir(sidecar_path));
    stage = 1;
    dialog.findChild<QPushButton*>(QStringLiteral("rawOpenButton"))->click();
  });
  prompt_timer.stop();
  CHECK(QDir().rmdir(sidecar_path));
  if (prompt_error) std::rethrow_exception(prompt_error);
  CHECK(outcome.has_value());
  CHECK(stage == 2);
  patchy::raw::DevelopParams precise;
  precise.white_balance = patchy::raw::WhiteBalanceMode::Custom;
  precise.custom_white_balance = {4300.123, 12.345};
  precise.exposure_ev = 0.12345;
  CHECK(save_raw_develop_settings(path, precise, {}).isEmpty());
  const auto saved = raw_test_bytes(sidecar_path);
  bool opened = false;
  CHECK(raw_test_dialog(path, [&](QDialog& dialog) {
    if (opened || !dialog.property("rawInfoReady").toBool()) return;
    opened = true;
    dialog.findChild<QPushButton*>(QStringLiteral("rawOpenButton"))->click();
  }).has_value());
  CHECK(raw_test_bytes(sidecar_path) == saved);
  raw_test_write(sidecar_path, QByteArray("unsupported"));
  opened = false;
  CHECK(raw_test_dialog(path, [&](QDialog& dialog) {
    CHECK(!dialog.findChild<QLabel*>(QStringLiteral("rawSettingsNotice"))->text().isEmpty());
    if (opened || !dialog.property("rawInfoReady").toBool()) return;
    opened = true;
    dialog.findChild<QPushButton*>(QStringLiteral("rawOpenButton"))->click();
  }).has_value());
  CHECK(raw_test_bytes(sidecar_path) == QByteArray("unsupported"));
  CHECK(QFile::remove(sidecar_path));
}

void ui_zoomable_preview_downsampling_and_logical_dimensions() {
  using namespace patchy::ui;
  ZoomableImagePreview preview;
  preview.resize(600, 400);
  QImage source(2400, 1600, QImage::Format_RGB32);
  for (int y = 0; y < source.height(); ++y)
    for (int x = 0; x < source.width(); ++x)
      source.setPixel(x, y, ((x / 2 + y / 2) & 1) ? qRgb(240, 20, 80) : qRgb(0, 220, 160));
  preview.set_image(source);
  preview.show();
  CHECK(process_events_until([&] { return preview.property("previewScaleReady").toBool(); }, 10000));
  const auto captured = preview.grab().toImage();
  // Compare painted pixels to a full-area reduction, including fractional DPI ratios.
  // The 2x2 blocks alias strongly under direct QPainter shrinking.
  const auto reference = source.scaled(captured.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
  for (int y = captured.height() / 2 - 20; y < captured.height() / 2 + 20; ++y)
    for (int x = captured.width() / 2 - 20; x < captured.width() / 2 + 20; ++x) {
      const auto color = captured.pixel(x, y), expected = reference.pixel(x, y);
      CHECK(std::abs(qRed(color) - qRed(expected)) <= 1);
      CHECK(std::abs(qGreen(color) - qGreen(expected)) <= 1);
      CHECK(std::abs(qBlue(color) - qBlue(expected)) <= 1);
    }
  const auto repeated = preview.grab().toImage();
  CHECK(repeated == captured);
  preview.zoom_to(0.125);
  preview.zoom_to(0.25);
  CHECK(process_events_until([&] { return preview.property("previewScaleReady").toBool(); }, 10000));
  preview.zoom_to(1.0, QPointF(170, 120));
  const auto pan = preview.property("previewPanOffset");
  preview.set_image(source.scaled(1200, 800), source.size());
  CHECK(preview.zoom() == 1.0);
  CHECK(preview.property("previewPanOffset") == pan);
  preview.set_image(source);
  CHECK(preview.zoom() == 1.0);
  CHECK(preview.property("previewPanOffset") == pan);
  source.fill(qRgb(40, 80, 160));
  preview.set_image(source);
  preview.zoom_to_fit();
  CHECK(process_events_until([&] { return preview.property("previewScaleReady").toBool(); }, 10000));
  const auto changed = preview.grab().toImage();
  CHECK(changed.pixel(changed.width() / 2, changed.height() / 2) == qRgb(40, 80, 160));
  save_widget_artifact("ui_raw_preview_downsampling", preview);
  // Premultiplied alpha averaging must not introduce the transparent pixels' red.
  QImage alpha(2400, 1600, QImage::Format_ARGB32);
  for (int y = 0; y < alpha.height(); ++y)
    for (int x = 0; x < alpha.width(); ++x)
      alpha.setPixel(x, y, ((x + y) & 1) ? qRgba(255, 0, 0, 0) : qRgba(0, 0, 255, 255));
  preview.set_image(alpha);
  CHECK(process_events_until([&] { return preview.property("previewScaleReady").toBool(); }, 10000));
  const auto transparent = preview.grab().toImage();
  const auto center = transparent.pixel(transparent.width() / 2, transparent.height() / 2);
  CHECK(std::abs(qRed(center) - qGreen(center)) <= 1);
  CHECK(qBlue(center) > qRed(center) + 100);
}

void ui_raw_local_photo_visual_acceptance_if_available() {
  using namespace patchy::ui;
  int checked = 0;
  for (const bool shelves : {false, true}) {
    const auto source = patchy::test::source_root_path() / "local-test-fixtures" / "raw" /
        (shelves ? "sony_fx30_fx300354.arw" : "sony_fx30_fx300416.arw");
    if (!std::filesystem::exists(source)) {
      std::cout << "  note: optional fixture missing: " << patchy::test::fixture_display_path(source) << '\n';
      continue;
    }
    ++checked;
    const auto path = to_qstring(source);
    const auto original = raw_test_bytes(path);
    bool captured_draft = false, captured_progress = false;
    CHECK(!raw_test_dialog(path, [&](QDialog& dialog) {
      auto* preview = dynamic_cast<ZoomableImagePreview*>(dialog.findChild<QWidget*>(QStringLiteral("rawDevelopPreview")));
      if (!captured_draft && preview->image().size() == (shelves ? QSize(1040, 695) : QSize(695, 1040)) &&
          preview->property("previewScaleReady").toBool()) {
        save_widget_artifact(shelves ? "ui_raw_sony_fx30_shelves_quick" : "ui_raw_sony_fx30_quick", dialog);
        captured_draft = true;
      }
      const int percent = dialog.property("rawProgressPercent").toInt();
      if (!captured_progress && dialog.property("rawProgressAccurate").toBool() && percent >= 20 && percent < 100) {
        save_widget_artifact(shelves ? "ui_raw_sony_fx30_shelves_refining" : "ui_raw_sony_fx30_refining", dialog);
        captured_progress = true;
      }
      if (!dialog.property("rawPreviewAccurate").toBool() || !preview->property("previewScaleReady").toBool()) return;
      CHECK(preview->image().size() == (shelves ? QSize(6240, 4168) : QSize(4168, 6240)));
      CHECK(dialog.findChild<QSlider*>(QStringLiteral("rawDenoiseSlider"))->value() == (shelves ? 332 : 364));
      CHECK(dialog.findChild<QComboBox*>(QStringLiteral("rawFbddCombo"))->currentData().toString() == QStringLiteral("full"));
      CHECK(dialog.findChild<QComboBox*>(QStringLiteral("rawColorNoiseCombo"))->currentData().toInt() == 2);
      CHECK(dialog.findChild<QComboBox*>(QStringLiteral("rawProfileCombo"))->currentData().toString() == QStringLiteral("natural"));
      save_widget_artifact(shelves ? "ui_raw_sony_fx30_shelves_fit" : "ui_raw_sony_fx30_fit", dialog);
      preview->zoom_to(1.0);
      save_widget_artifact(shelves ? "ui_raw_sony_fx30_shelves_100" : "ui_raw_sony_fx30_100", dialog);
      dialog.reject();
    }));
    CHECK(captured_draft && captured_progress);
    CHECK(raw_test_bytes(path) == original);
  }
  if (checked == 0) {
    patchy::test::skip("no optional fixture present (local-test-fixtures/raw Sony FX30 photos)");
  }
}

void ui_raw_develop_dialog_accept_opens_document_and_save_routes_to_psd() {
  RawDevelopSettingsSanitizer raw_settings_sanitizer;
  SettingsValueRestorer dialog_restorer(QStringLiteral("imports/showRawDevelopDialog"));
  {
    auto settings = patchy::ui::app_settings();
    settings.setValue(QStringLiteral("imports/showRawDevelopDialog"), true);
    settings.sync();
  }
  const auto path = write_raw_dng_fixture(QStringLiteral("raw_develop_accept.dng"));
  patchy::ui::MainWindow window;
  show_window(window);

  auto clicked = std::make_shared<bool>(false);
  auto step = std::make_shared<std::function<bool()>>();
  *step = [clicked] {
    auto* dialog = find_top_level_dialog(QStringLiteral("rawDevelopDialog"));
    if (dialog == nullptr) {
      return false;
    }
    auto* open_button = dialog->findChild<QPushButton*>(QStringLiteral("rawOpenButton"));
    if (open_button == nullptr || !open_button->isEnabled()) {
      return false;
    }
    open_button->click();
    *clicked = true;
    return true;
  };
  drive_raw_develop_dialog(step);
  patchy::ui::MainWindowTestAccess::open_document_path(window, path);
  CHECK(*clicked);
  CHECK(!QFileInfo::exists(patchy::ui::raw_develop_settings_path(path)));

  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  CHECK(document.width() == 128);
  CHECK(document.height() == 96);
  CHECK(document.layers().size() == 1);
  CHECK(patchy::ui::MainWindowTestAccess::active_session_path(window) == path);
  // Photoshop parity: the developed document is clean; the raw file on disk stays the
  // untouched source, so closing without editing must not prompt.
  CHECK(!patchy::ui::MainWindowTestAccess::active_session_is_modified(window));
  save_widget_artifact("ui_raw_developed_document", window);

  // Save can never write the raw back: it must route to Save As defaulting to
  // <basename>.psd (the read-only-source counterpart of the layered-JPEG routing).
  bool saw_dialog = false;
  QString default_name;
  QTimer::singleShot(0, [&] {
    auto* dialog = qobject_cast<QFileDialog*>(find_top_level_dialog(QStringLiteral("saveAsFileDialog")));
    CHECK(dialog != nullptr);
    const auto selected = dialog->selectedFiles();
    if (!selected.isEmpty()) {
      default_name = QFileInfo(selected.first()).fileName();
    }
    saw_dialog = true;
    dialog->reject();
  });
  CHECK(!patchy::ui::MainWindowTestAccess::save_document(window));
  CHECK(saw_dialog);
  CHECK(default_name == QStringLiteral("raw_develop_accept.psd"));
}

void ui_raw_file_drop_returns_before_dialog_and_preserves_batch() {
  using patchy::ui::MainWindowTestAccess;
  SettingsValueRestorer dialog_restorer(QStringLiteral("imports/showRawDevelopDialog"));
  auto settings = patchy::ui::app_settings();
  settings.setValue(QStringLiteral("imports/showRawDevelopDialog"), true);
  settings.sync();

  const auto stem = patchy::test::utf8_string(patchy::test::kUnicodeCombinedStem);
  const auto path = write_raw_dng_fixture(QStringLiteral("drop ") +
      QString::fromUtf8(stem.data(), static_cast<qsizetype>(stem.size())) + QStringLiteral(".dng"));
  const auto original_bytes = raw_test_bytes(path);
  const auto png_path = path + QStringLiteral(".png");
  QImage source(6, 4, QImage::Format_RGB32);
  source.fill(QColor(20, 80, 160));
  CHECK(source.save(png_path));

  for (const bool drop_on_tabs : {false, true}) {
    for (const bool open_raw : {false, true}) {
      patchy::ui::MainWindow window;
      show_window_empty(window);
      auto* tabs = window.findChild<QTabWidget*>(QStringLiteral("documentTabs"));
      CHECK(tabs != nullptr);
      QWidget* target = drop_on_tabs ? static_cast<QWidget*>(tabs) : &window;
      bool drop_returned = false;
      bool dialog_seen = false;
      bool clicked_open = false;
      std::exception_ptr error;
      QElapsedTimer elapsed;
      elapsed.start();
      QTimer driver;
      QObject::connect(&driver, &QTimer::timeout, [&] {
        auto* dialog = find_top_level_dialog(QStringLiteral("rawDevelopDialog"));
        if (!dialog) return;
        try {
          // The native source can release its drag loop before any modal UI.
          CHECK(drop_returned);
          CHECK(elapsed.elapsed() < kRawDialogDeadlineMs);
          CHECK(MainWindowTestAccess::session_count(window) == 0);
          CHECK(dialog->windowTitle().contains(QFileInfo(path).fileName()));
          dialog_seen = true;
          if (!open_raw) {
            dialog->reject();
          } else if (!clicked_open) {
            auto* button = dialog->findChild<QPushButton*>(QStringLiteral("rawOpenButton"));
            CHECK(button != nullptr);
            if (button->isEnabled()) {
              clicked_open = true;
              button->click();
            }
          }
        } catch (...) {
          error = std::current_exception();
          dialog->reject();
          driver.stop();
        }
      });
      driver.start(10);
      {
        QMimeData mime;
        mime.setUrls({QUrl::fromLocalFile(path), QUrl::fromLocalFile(png_path)});
        QDragEnterEvent enter(QPoint(50, 50), Qt::CopyAction | Qt::MoveAction, &mime,
                              Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(target, &enter);
        CHECK(enter.isAccepted());
        QDropEvent drop(QPointF(50, 50), Qt::CopyAction | Qt::MoveAction, &mime,
                        Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(target, &drop);
        drop_returned = true;
        CHECK(drop.isAccepted());
        CHECK(drop.dropAction() == Qt::CopyAction);
      }  // Native events and mime data are gone before processing starts.
      CHECK(!dialog_seen);
      CHECK(MainWindowTestAccess::session_count(window) == 0);
      QApplication::processEvents();
      driver.stop();
      if (error) std::rethrow_exception(error);
      CHECK(dialog_seen);
      CHECK(clicked_open == open_raw);
      CHECK(tabs->count() == (open_raw ? 2 : 1));
      if (open_raw) CHECK(tabs->tabText(0) == QFileInfo(path).fileName());
      CHECK(tabs->tabText(tabs->count() - 1) == QFileInfo(png_path).fileName());
      CHECK(QDir::fromNativeSeparators(MainWindowTestAccess::active_session_path(window)) == png_path);
      CHECK(raw_test_bytes(path) == original_bytes);
      CHECK(!QFileInfo::exists(patchy::ui::raw_develop_settings_path(path)));
    }
  }
}

void ui_raw_develop_dialog_cancel_aborts_open() {
  RawDevelopSettingsSanitizer raw_settings_sanitizer;
  SettingsValueRestorer dialog_restorer(QStringLiteral("imports/showRawDevelopDialog"));
  {
    auto settings = patchy::ui::app_settings();
    settings.setValue(QStringLiteral("imports/showRawDevelopDialog"), true);
    settings.sync();
  }
  const auto path = write_raw_dng_fixture(QStringLiteral("raw_develop_cancel.dng"));
  patchy::ui::MainWindow window;
  patchy::Document starter(24, 18, patchy::PixelFormat::rgb8());
  starter.add_pixel_layer("Base", solid_pixels(24, 18, patchy::PixelFormat::rgb8(), QColor(10, 20, 30)));
  window.add_document_session(std::move(starter), QStringLiteral("Starter"));
  show_window(window);

  auto step = std::make_shared<std::function<bool()>>();
  *step = [] {
    auto* dialog = find_top_level_dialog(QStringLiteral("rawDevelopDialog"));
    if (dialog == nullptr) {
      return false;
    }
    dialog->reject();
    return true;
  };
  drive_raw_develop_dialog(step);
  patchy::ui::MainWindowTestAccess::open_document_path(window, path);

  // The cancelled open added nothing: the starter document is still active and untouched.
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  CHECK(document.width() == 24);
  CHECK(document.height() == 18);
  CHECK(patchy::ui::MainWindowTestAccess::active_session_path(window).isEmpty());
}

void ui_raw_develop_dialog_exposure_slider_brightens_preview() {
  RawDevelopSettingsSanitizer raw_settings_sanitizer;
  SettingsValueRestorer dialog_restorer(QStringLiteral("imports/showRawDevelopDialog"));
  {
    auto settings = patchy::ui::app_settings();
    settings.setValue(QStringLiteral("imports/showRawDevelopDialog"), true);
    settings.sync();
  }
  const auto path = write_raw_dng_fixture(QStringLiteral("raw_develop_preview.dng"));
  patchy::ui::MainWindow window;
  show_window(window);

  const auto mean_green = [](const QImage& image) {
    double total = 0.0;
    for (int y = 0; y < image.height(); ++y) {
      for (int x = 0; x < image.width(); ++x) {
        total += qGreen(image.pixel(x, y));
      }
    }
    return total / (static_cast<double>(image.width()) * image.height());
  };

  auto stage = std::make_shared<int>(0);
  auto last_image_key = std::make_shared<qint64>(0);
  auto base_mean = std::make_shared<double>(0.0);
  auto bright_mean = std::make_shared<double>(0.0);
  auto step = std::make_shared<std::function<bool()>>();
  *step = [=] {
    auto* dialog = find_top_level_dialog(QStringLiteral("rawDevelopDialog"));
    if (dialog == nullptr) {
      return false;
    }
    // ZoomableImagePreview has no Q_OBJECT macro, so findChild cannot cast to it.
    auto* preview = static_cast<patchy::ui::ZoomableImagePreview*>(
        dialog->findChild<QWidget*>(QStringLiteral("rawDevelopPreview")));
    auto* status = dialog->findChild<QLabel*>(QStringLiteral("rawDevelopStatus"));
    auto* exposure = dialog->findChild<QSlider*>(QStringLiteral("rawExposureSlider"));
    if (preview == nullptr || status == nullptr || exposure == nullptr) {
      return false;
    }
    const auto& image = preview->image();
    switch (*stage) {
      case 0:
        // Wait for the first develop: the accurate preview uses full output size (128x96). The
        // sanitized defaults have auto-brighten off, so the exposure shift below is
        // monotonic with no extra setup.
        if (image.isNull() || image.width() != 128 || !status->text().isEmpty()) {
          return false;
        }
        *last_image_key = image.cacheKey();
        *base_mean = mean_green(image);
        exposure->setValue(150);  // +1.5 EV
        *stage = 1;
        return false;
      case 1:
        if (image.isNull() || image.cacheKey() == *last_image_key || !status->text().isEmpty()) {
          return false;
        }
        *bright_mean = mean_green(image);
        {
          // Every group's sliders start at one x (GitHub issue 392 follow-up).
          const auto left = [dialog](const char* name) {
            auto* widget = dialog->findChild<QWidget*>(QLatin1String(name));
            CHECK(widget != nullptr);
            return widget == nullptr ? -1 : widget->mapTo(dialog, QPoint(0, 0)).x();
          };
          CHECK(left("rawExposureSlider") == left("rawTemperatureSlider"));
          CHECK(left("rawSaturationSlider") == left("rawTemperatureSlider"));
          CHECK(left("rawDenoiseSlider") == left("rawTemperatureSlider"));
        }
        save_widget_artifact("ui_raw_develop_dialog", *dialog);
        *stage = 2;
        dialog->reject();
        return true;
    }
    return true;
  };
  drive_raw_develop_dialog(step);
  patchy::ui::MainWindowTestAccess::open_document_path(window, path);

  CHECK(*stage == 2);
  CHECK(*base_mean > 10.0);
  CHECK(*bright_mean > *base_mean + 5.0);
}

void ui_raw_preference_disabled_opens_with_camera_defaults() {
  RawDevelopSettingsSanitizer raw_settings_sanitizer;
  SettingsValueRestorer dialog_restorer(QStringLiteral("imports/showRawDevelopDialog"));
  {
    auto settings = patchy::ui::app_settings();
    settings.setValue(QStringLiteral("imports/showRawDevelopDialog"), false);
    settings.sync();
  }
  const auto path = write_raw_dng_fixture(QStringLiteral("raw_develop_silent.dng"));
  patchy::ui::MainWindow window;
  show_window(window);

  auto saw_dialog = std::make_shared<bool>(false);
  auto step = std::make_shared<std::function<bool()>>();
  *step = [saw_dialog] {
    if (find_top_level_dialog(QStringLiteral("rawDevelopDialog")) != nullptr) {
      *saw_dialog = true;
      return true;
    }
    return false;
  };
  drive_raw_develop_dialog(step, 300);
  patchy::ui::MainWindowTestAccess::open_document_path(window, path);

  CHECK(!*saw_dialog);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  CHECK(document.width() == 128);
  CHECK(document.height() == 96);
  CHECK(patchy::ui::MainWindowTestAccess::active_session_path(window) == path);
}

void ui_heif_open_is_read_only_if_available() {
  const auto path = QString::fromStdWString(
      patchy::test::committed_format_fixture_path("heif", "quadrants.heic").wstring());
  CHECK(QFileInfo::exists(path));

  patchy::ui::MainWindow window;
  show_window(window);

  // Machines without a platform HEIC decoder (a Linux box without libheif or its HEVC
  // plug-in; Windows may lack the Store codec packages) raise the open-failed box,
  // which would hang the offscreen suite without this repeating dismisser. Dismiss via
  // reject() so the Microsoft Store button can never be triggered from a test.
  bool saw_error = false;
  QString error_text;
  int poll_attempts = 0;
  QTimer poller;
  QObject::connect(&poller, &QTimer::timeout, [&saw_error, &error_text, &poll_attempts, &poller] {
    if (++poll_attempts > 500) {
      poller.stop();
      return;
    }
    for (auto* widget : QApplication::topLevelWidgets()) {
      auto* box = qobject_cast<QMessageBox*>(widget);
      if (box != nullptr && box->objectName() == QStringLiteral("openFailedMessageBox") && box->isVisible()) {
        saw_error = true;
        error_text = box->text();
        box->reject();
        poller.stop();
        return;
      }
    }
  });
  poller.start(10);
  patchy::ui::MainWindowTestAccess::open_document_path(window, path);
  QApplication::processEvents();
  poller.stop();

  if (saw_error) {
    // Only a missing platform decoder is an acceptable failure; note that the marker
    // prefix has already been stripped for display by then.
    const bool codec_unavailable = error_text.contains(QStringLiteral("Microsoft Store")) ||
                                   error_text.contains(QStringLiteral("system codec")) ||
                                   error_text.contains(QStringLiteral("Flatpak codec extension")) ||
                                   error_text.contains(QStringLiteral("cannot decode HEIC images"));  // Linux libheif
    CHECK(codec_unavailable);
    patchy::test::skip("HEIC platform decoder unavailable: " + error_text.toStdString());
  }

  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  CHECK(document.width() == 64);
  CHECK(document.height() == 48);
  CHECK(patchy::ui::MainWindowTestAccess::active_session_path(window) == path);
  CHECK(!patchy::ui::MainWindowTestAccess::active_session_is_modified(window));
  // Same layer name on every platform, whichever decode path ran (WIC, libheif or Qt).
  CHECK(std::as_const(document).layers().front().name() == "Background");
  // Quadrant sanity on the decoded pixels (fixture: red / green / blue / white).
  const auto& pixels = std::as_const(document.layers().front()).pixels();
  CHECK(pixels.pixel(10, 10)[0] > 200);
  CHECK(pixels.pixel(54, 10)[1] > 200);
  CHECK(pixels.pixel(10, 40)[2] > 200);
  save_widget_artifact("ui_heif_opened_document", window);

  // HEIC is a read-only source: Save must route to Save As defaulting <basename>.psd
  // (the camera-raw routing, driven by the registry handler having no writer).
  bool saw_dialog = false;
  QString default_name;
  QTimer::singleShot(0, [&] {
    auto* dialog = qobject_cast<QFileDialog*>(find_top_level_dialog(QStringLiteral("saveAsFileDialog")));
    CHECK(dialog != nullptr);
    const auto selected = dialog->selectedFiles();
    if (!selected.isEmpty()) {
      default_name = QFileInfo(selected.first()).fileName();
    }
    saw_dialog = true;
    dialog->reject();
  });
  CHECK(!patchy::ui::MainWindowTestAccess::save_document(window));
  CHECK(saw_dialog);
  CHECK(default_name == QStringLiteral("quadrants.psd"));
}

// --- Develop Raw histogram, Shift/Alt-click and zoom controls (GitHub issues 393, 394, 395) ---

patchy::ui::RawPreviewHistogram raw_test_luminance_histogram(
    std::initializer_list<std::pair<int, std::uint32_t>> bins) {
  patchy::ui::RawPreviewHistogram histogram;
  for (const auto& [bin, count] : bins) {
    histogram.luminance[static_cast<std::size_t>(bin)] += count;
    histogram.samples += count;
  }
  return histogram;
}

QLabel* raw_test_row_label(QDialog& dialog, const QString& text) {
  for (auto* label : dialog.findChildren<QLabel*>()) {
    if (label->text() == text) return label;
  }
  return nullptr;
}

void ui_raw_histogram_regions_and_auto_values() {
  using namespace patchy::ui;
  using Region = RawHistogramRegion;
  CHECK(raw_histogram_region_at(0.0) == Region::Shadows);
  CHECK(raw_histogram_region_at(0.29) == Region::Shadows);
  CHECK(raw_histogram_region_at(0.30) == Region::Exposure);
  CHECK(raw_histogram_region_at(0.64) == Region::Exposure);
  CHECK(raw_histogram_region_at(0.65) == Region::Highlights);
  CHECK(raw_histogram_region_at(1.0) == Region::Highlights);
  CHECK(raw_histogram_region_at(-0.01) == Region::None);
  CHECK(raw_histogram_region_at(1.01) == Region::None);
  CHECK(raw_histogram_region_token(Region::Exposure) == QStringLiteral("exposure"));

  // Counting: luminance uses the integer 30/59/11 weights.
  const std::array<std::uint8_t, 18> pixels = {255, 0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 128, 128, 128};
  const auto small = raw_preview_histogram(pixels, 3, 2);
  CHECK(small.samples == 6);
  CHECK(small.red[255] == 2 && small.green[255] == 2 && small.blue[255] == 2);
  CHECK(small.luminance[77] == 1 && small.luminance[150] == 1 && small.luminance[28] == 1);
  CHECK(small.luminance[0] == 1 && small.luminance[255] == 1 && small.luminance[128] == 1);
  CHECK(raw_preview_histogram(pixels, 4, 2).samples == 0);
  // A large develop is read on a stride, bounded by kRawHistogramMaxSamples.
  std::vector<std::uint8_t> large(static_cast<std::size_t>(3000) * 2000 * 3, 90);
  const auto strided = raw_preview_histogram(large, 3000, 2000);
  CHECK(strided.samples > 0 && strided.samples <= kRawHistogramMaxSamples);
  CHECK(strided.luminance[90] == strided.samples);

  // Empty and flat histograms have no automatic value.
  const RawToneModel neutral(patchy::raw::RenderingProfile::Neutral, patchy::raw::kProcessingVersion);
  const RawToneModel natural(patchy::raw::RenderingProfile::Natural, patchy::raw::kProcessingVersion);
  CHECK(!raw_auto_tone({}, neutral));
  CHECK(!raw_auto_tone(raw_test_luminance_histogram({{128, 5000}}), neutral));

  // The model: neutral sliders pass values through either profile; Exposure brightens and darkens,
  // and LibRaw's highlight shoulder keeps +1 EV from clipping a value a plain gain would clip.
  for (const double x : {0.0, 0.1, 0.5, 0.9, 1.0}) {
    CHECK(std::abs(neutral.map(x, {}) - x) < 1e-9);
    CHECK(std::abs(natural.map(x, {}) - x) < 1.0 / 255.0);
  }
  CHECK(neutral.map(0.3, {1.0, 0.0, 0.0, 0.0}) > 0.3 && neutral.map(0.3, {-1.0, 0.0, 0.0, 0.0}) < 0.3);
  CHECK(RawToneModel::expose(0.8, 1.0) < 1.0);
  CHECK(std::abs(RawToneModel::expose(0.2, 1.0) - 0.2) > 0.05);

  // A full-range image needs nothing.
  const auto full = raw_test_luminance_histogram({{0, 20}, {40, 1000}, {128, 3000}, {200, 2000}, {255, 20}});
  CHECK(raw_auto_tone(full, natural) == RawToneAutoValues{});

  // A dark, narrow image: the four values together carry its clip points to black and white,
  // like Auto Tone, in the sliders' own steps; the same baseline always gives the same values.
  const auto dark = raw_test_luminance_histogram({{10, 1000}, {60, 3000}, {120, 4000}, {190, 2000}});
  for (const auto* model : {&neutral, &natural}) {
    const auto values = raw_auto_tone(dark, *model);
    CHECK(values.has_value());
    std::cout << "  dark auto: " << values->exposure_ev << " EV, contrast " << values->contrast << ", highlights "
              << values->highlights << ", shadows " << values->shadows << ", maps 10/120/190 to "
              << model->map(10.0 / 255.0, *values) * 255.0 << "/" << model->map(120.0 / 255.0, *values) * 255.0
              << "/" << model->map(190.0 / 255.0, *values) * 255.0 << '\n';
    CHECK(values->exposure_ev > 0.0 && values->exposure_ev <= 3.0);
    CHECK(std::abs(values->exposure_ev * 100.0 - std::round(values->exposure_ev * 100.0)) < 1e-9);
    CHECK(values->contrast == std::round(values->contrast) && std::abs(values->contrast) <= 100.0);
    CHECK(model->map(10.0 / 255.0, *values) < 0.1);
    CHECK(model->map(190.0 / 255.0, *values) > 0.9);
    CHECK(raw_auto_tone(dark, *model) == values);
  }
}

void ui_raw_dialog_histogram_drag_and_modifier_clicks() {
  using namespace patchy::ui;
  const auto path = write_raw_dng_fixture(QStringLiteral("raw_histogram_controls.dng"));
  // A dark horizontal ramp, so the histogram spans the shadows and every auto value moves.
  patchy::test::SyntheticDngOptions ramp;
  ramp.horizontal_ramp = true;
  ramp.red_value = ramp.green_value = ramp.blue_value = 3000;
  const auto ramp_bytes = patchy::test::synthetic_bayer_dng(128, 96, ramp);
  raw_test_write(path, QByteArray(reinterpret_cast<const char*>(ramp_bytes.data()),
                                  static_cast<qsizetype>(ramp_bytes.size())));
  bool checked = false;
  int stage = 0;
  int auto_exposure = 0;
  int auto_contrast = 0;
  CHECK(!raw_test_dialog(path, [&](QDialog& dialog) {
    if (checked) return;
    auto* histogram = static_cast<RawHistogramWidget*>(dialog.findChild<QWidget*>(QStringLiteral("rawHistogram")));
    auto* region_label = dialog.findChild<QLabel*>(QStringLiteral("rawHistogramRegionLabel"));
    CHECK(histogram != nullptr && region_label != nullptr);
    if (!dialog.property("rawPreviewAccurate").toBool() || !histogram->histogram() ||
        dialog.property("rawAutoMeasuring").toBool())
      return;
    CHECK(histogram->property("rawHistogramSamples").toULongLong() == 128ULL * 96ULL);
    auto* exposure = dialog.findChild<QSlider*>(QStringLiteral("rawExposureSlider"));
    auto* contrast = dialog.findChild<QSlider*>(QStringLiteral("rawContrastSlider"));
    auto* shadows = dialog.findChild<QSlider*>(QStringLiteral("rawShadowsSlider"));
    auto* saturation = dialog.findChild<QSlider*>(QStringLiteral("rawSaturationSlider"));
    auto* brightness = dialog.findChild<QSlider*>(QStringLiteral("rawBrightnessSlider"));
    auto* wb = dialog.findChild<QComboBox*>(QStringLiteral("rawWhiteBalanceCombo"));
    const auto click = [](QWidget& widget, Qt::KeyboardModifiers modifiers) {
      const auto center = widget.rect().center();
      send_mouse(widget, QEvent::MouseButtonPress, center, Qt::LeftButton, Qt::LeftButton, modifiers);
      send_mouse(widget, QEvent::MouseButtonRelease, center, Qt::LeftButton, Qt::NoButton, modifiers);
    };

    // Shift-click gives one auto value, measured from the develop with the Tone sliders at their
    // defaults: it repeats after the preview refreshes, whatever the slider held before.
    auto* contrast_label = raw_test_row_label(dialog, QObject::tr("Contrast:"));
    if (stage == 0) {
      click(*exposure, Qt::ShiftModifier);
      CHECK(!dialog.property("rawAutoMeasuring").toBool());  // the default preview was the baseline
      auto_exposure = exposure->value();
      CHECK(auto_exposure > 0);
      click(*contrast_label, Qt::ShiftModifier);
      auto_contrast = contrast->value();
      stage = 1;
      return;
    }
    if (stage == 1) {
      click(*exposure, Qt::ShiftModifier);
      CHECK(exposure->value() == auto_exposure);
      click(*contrast_label, Qt::ShiftModifier);
      CHECK(contrast->value() == auto_contrast);
      exposure->setValue(200);
      stage = 2;
      return;
    }
    if (stage == 2 || stage == 3) {
      click(*exposure, Qt::ShiftModifier);
      CHECK(exposure->value() == auto_exposure);
      if (stage == 2) {
        exposure->setValue(-200);
        stage = 3;
        return;
      }
      // A setting outside the Tone sliders changes the baseline: it is measured again first.
      saturation->setValue(40);
      exposure->setValue(-200);
      click(*exposure, Qt::ShiftModifier);
      CHECK(dialog.property("rawAutoMeasuring").toBool() && exposure->value() == -200);
      stage = 4;
      return;
    }
    const auto remeasured = exposure->value();
    CHECK(remeasured != -200 && std::abs(remeasured - auto_exposure) <= 40);
    click(*exposure, Qt::ShiftModifier);
    CHECK(exposure->value() == remeasured && !dialog.property("rawAutoMeasuring").toBool());
    checked = true;
    // No automatic value: Shift-click leaves Saturation and Brightness alone.
    saturation->setValue(30);
    click(*saturation, Qt::ShiftModifier);
    CHECK(saturation->value() == 30);
    brightness->setValue(150);
    click(*raw_test_row_label(dialog, QObject::tr("Brightness:")), Qt::ShiftModifier);
    CHECK(brightness->value() == 150);
    // Alt-click resets, on the slider, its name or its value label.
    click(*raw_test_row_label(dialog, QObject::tr("Saturation:")), Qt::AltModifier);
    CHECK(saturation->value() == 0);
    click(*dialog.findChild<QLabel*>(QStringLiteral("rawBrightnessSliderValue")), Qt::AltModifier);
    CHECK(brightness->value() == 100);
    // White balance: Shift is Auto, Alt is As Shot.
    click(*raw_test_row_label(dialog, QObject::tr("Temperature:")), Qt::ShiftModifier);
    CHECK(wb->currentData().toString() == QStringLiteral("auto"));
    click(*raw_test_row_label(dialog, QObject::tr("Tint:")), Qt::AltModifier);
    CHECK(wb->currentData().toString() == QStringLiteral("asShot"));
    CHECK(exposure->toolTip().contains(QStringLiteral("Shift-click")));
    CHECK(saturation->toolTip().contains(QStringLiteral("-click")) && !saturation->toolTip().contains(QStringLiteral("Shift")));

    // Hovering names the region's setting; dragging there moves its slider.
    const auto plot = histogram->plot_rect();
    const auto at = [&](double fraction) {
      return QPoint(plot.left() + static_cast<int>(std::lround(fraction * plot.width())), plot.center().y());
    };
    send_mouse(*histogram, QEvent::MouseMove, at(0.1), Qt::NoButton, Qt::NoButton);
    CHECK(histogram->property("rawHistogramRegion").toString() == QStringLiteral("shadows"));
    CHECK(region_label->text().startsWith(QObject::tr("Shadows")));
    send_mouse(*histogram, QEvent::MouseMove, at(0.5), Qt::NoButton, Qt::NoButton);
    CHECK(histogram->property("rawHistogramRegion").toString() == QStringLiteral("exposure"));
    CHECK(region_label->text().startsWith(QObject::tr("Exposure")));
    CHECK(region_label->text().contains(QObject::tr("EV")));
    send_mouse(*histogram, QEvent::MouseMove, at(0.9), Qt::NoButton, Qt::NoButton);
    CHECK(histogram->property("rawHistogramRegion").toString() == QStringLiteral("highlights"));

    const auto start = exposure->value();
    const auto from = at(0.5);
    const auto to = from + QPoint(plot.width() / 4, 7);
    send_mouse(*histogram, QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton);
    CHECK(exposure->isSliderDown());
    send_mouse(*histogram, QEvent::MouseMove, to, Qt::NoButton, Qt::LeftButton);
    const auto expected = std::clamp(
        start + static_cast<int>(std::lround(static_cast<double>(to.x() - from.x()) / plot.width() * 250.0)),
        exposure->minimum(), exposure->maximum());
    CHECK(exposure->value() == expected);
    CHECK(histogram->property("rawHistogramDragging").toBool());
    // The pointer leaving the Exposure region mid-drag keeps adjusting Exposure.
    send_mouse(*histogram, QEvent::MouseMove, at(0.95), Qt::NoButton, Qt::LeftButton);
    CHECK(region_label->text().startsWith(QObject::tr("Exposure")));
    send_mouse(*histogram, QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton);
    CHECK(exposure->value() == expected);
    CHECK(!exposure->isSliderDown() && !histogram->property("rawHistogramDragging").toBool());
    // Dragging left in the Shadows region lowers Shadows, clamped to its range.
    send_mouse(*histogram, QEvent::MouseButtonPress, at(0.2), Qt::LeftButton, Qt::LeftButton);
    send_mouse(*histogram, QEvent::MouseMove, at(0.2) - QPoint(plot.width() * 3, 0), Qt::NoButton, Qt::LeftButton);
    send_mouse(*histogram, QEvent::MouseButtonRelease, at(0.2) - QPoint(plot.width() * 3, 0), Qt::LeftButton, Qt::NoButton);
    CHECK(shadows->value() == shadows->minimum());
    dialog.reject();
  }));
  CHECK(checked);
}

void ui_raw_dialog_zoom_controls_and_scrubby_zoom() {
  using namespace patchy::ui;
  SettingsValueRestorer scrubby_restorer(QStringLiteral("imports/rawDevelopScrubbyZoom"));
  {
    auto settings = app_settings();
    settings.setValue(QStringLiteral("imports/rawDevelopScrubbyZoom"), false);
    settings.sync();
  }
  const auto path = write_raw_dng_fixture(QStringLiteral("raw_zoom_controls.dng"));
  bool checked = false;
  CHECK(!raw_test_dialog(path, [&](QDialog& dialog) {
    if (checked || !dialog.property("rawPreviewAccurate").toBool()) return;
    checked = true;
    auto* preview = static_cast<ZoomableImagePreview*>(dialog.findChild<QWidget*>(QStringLiteral("rawDevelopPreview")));
    auto* combo = dialog.findChild<QComboBox*>(QStringLiteral("rawZoomCombo"));
    auto* zoom_100 = dialog.findChild<QPushButton*>(QStringLiteral("rawZoom100Button"));
    auto* zoom_fit = dialog.findChild<QPushButton*>(QStringLiteral("rawZoomFitButton"));
    auto* scrubby = dialog.findChild<QCheckBox*>(QStringLiteral("rawScrubbyZoomCheck"));
    auto* open = dialog.findChild<QPushButton*>(QStringLiteral("rawOpenButton"));
    CHECK(preview && combo && zoom_100 && zoom_fit && scrubby && open);
    CHECK(preview->fit_mode() && combo->currentText().startsWith(QObject::tr("Fit")));
    CHECK(!scrubby->isChecked() && !preview->scrubby_zoom());

    zoom_100->click();
    CHECK(!preview->fit_mode() && std::abs(preview->zoom() - 1.0) < 1e-9);
    CHECK(combo->currentText() == QStringLiteral("100%"));
    zoom_fit->click();
    CHECK(preview->fit_mode() && combo->currentText().startsWith(QObject::tr("Fit")));
    const auto index_200 = combo->findData(2.0);
    CHECK(index_200 > 0);
    emit combo->activated(index_200);
    CHECK(std::abs(preview->zoom() - 2.0) < 1e-9 && combo->currentText() == QStringLiteral("200%"));
    emit combo->activated(0);
    CHECK(preview->fit_mode());
    // A typed percentage zooms on Enter without pressing the default Open button.
    combo->lineEdit()->setFocus();
    combo->setEditText(QStringLiteral("150"));
    send_key(*combo->lineEdit(), Qt::Key_Return);
    CHECK(std::abs(preview->zoom() - 1.5) < 1e-9 && combo->currentText() == QStringLiteral("150%"));
    CHECK(open->isEnabled() && dialog.isVisible());
    // Wheel zoom off the presets shows its own percentage.
    preview->zoom_to(1.234);
    CHECK(combo->currentText() == QStringLiteral("123.4%"));

    // Left drag pans with Scrubby Zoom off.
    preview->zoom_to(8.0);
    const auto center = preview->rect().center();
    const auto pan_before = preview->property("previewPanOffset").toPoint();
    drag(*preview, center, center + QPoint(40, 0));
    CHECK(preview->property("previewPanOffset").toPoint() != pan_before);
    CHECK(std::abs(preview->zoom() - 8.0) < 1e-9);

    // Scrubby Zoom: a horizontal drag zooms about the press point, right = in.
    scrubby->click();
    CHECK(preview->scrubby_zoom() && app_settings().value(QStringLiteral("imports/rawDevelopScrubbyZoom")).toBool());
    preview->zoom_to(1.0);
    send_mouse(*preview, QEvent::MouseButtonPress, center, Qt::LeftButton, Qt::LeftButton);
    send_mouse(*preview, QEvent::MouseMove, center + QPoint(10, 0), Qt::NoButton, Qt::LeftButton);
    send_mouse(*preview, QEvent::MouseMove, center + QPoint(60, 25), Qt::NoButton, Qt::LeftButton);
    send_mouse(*preview, QEvent::MouseButtonRelease, center + QPoint(60, 25), Qt::LeftButton, Qt::NoButton);
    CHECK(std::abs(preview->zoom() - std::pow(1.01, 60.0)) < 1e-6);
    send_mouse(*preview, QEvent::MouseButtonPress, center, Qt::LeftButton, Qt::LeftButton);
    send_mouse(*preview, QEvent::MouseMove, center + QPoint(10, 0), Qt::NoButton, Qt::LeftButton);
    send_mouse(*preview, QEvent::MouseMove, center - QPoint(60, 0), Qt::NoButton, Qt::LeftButton);
    send_mouse(*preview, QEvent::MouseButtonRelease, center - QPoint(60, 0), Qt::LeftButton, Qt::NoButton);
    CHECK(std::abs(preview->zoom() - 1.0) < 1e-6);
    // A click steps in, an Alt-click steps out.
    send_mouse(*preview, QEvent::MouseButtonPress, center, Qt::LeftButton, Qt::LeftButton);
    send_mouse(*preview, QEvent::MouseButtonRelease, center, Qt::LeftButton, Qt::NoButton);
    CHECK(std::abs(preview->zoom() - 1.5) < 1e-9);
    send_mouse(*preview, QEvent::MouseButtonPress, center, Qt::LeftButton, Qt::LeftButton, Qt::AltModifier);
    send_mouse(*preview, QEvent::MouseButtonRelease, center, Qt::LeftButton, Qt::NoButton, Qt::AltModifier);
    CHECK(std::abs(preview->zoom() - 1.0) < 1e-9);
    // The middle button still pans while Scrubby Zoom is on.
    preview->zoom_to(8.0);
    const auto pan_scrubby = preview->property("previewPanOffset").toPoint();
    drag(*preview, center, center + QPoint(0, 30), Qt::NoModifier, Qt::MiddleButton);
    CHECK(preview->property("previewPanOffset").toPoint() != pan_scrubby);
    CHECK(std::abs(preview->zoom() - 8.0) < 1e-9);
    scrubby->click();
    CHECK(!preview->scrubby_zoom());
    dialog.reject();
  }));
  CHECK(checked);
}

// Issue screenshots on a real photo (local-test-fixtures/raw); skipped without it.
void ui_raw_dialog_issue_shots_if_available() {
  using namespace patchy::ui;
  const auto source = patchy::test::source_root_path() / "local-test-fixtures" / "raw" / "sony_fx30_fx300354.arw";
  if (!std::filesystem::exists(source)) {
    patchy::test::skip("optional fixture missing: local-test-fixtures/raw/sony_fx30_fx300354.arw");
  }
  SettingsValueRestorer scrubby_restorer(QStringLiteral("imports/rawDevelopScrubbyZoom"));
  {
    auto settings = app_settings();
    settings.setValue(QStringLiteral("imports/rawDevelopScrubbyZoom"), true);
    settings.sync();
  }
  const auto path = to_qstring(source);
  const auto sidecar = raw_develop_settings_path(path);
  const auto had_sidecar = QFileInfo::exists(sidecar);
  int stage = 0;
  CHECK(!raw_test_dialog(path, [&](QDialog& dialog) {
    auto* preview = static_cast<ZoomableImagePreview*>(dialog.findChild<QWidget*>(QStringLiteral("rawDevelopPreview")));
    auto* histogram = static_cast<RawHistogramWidget*>(dialog.findChild<QWidget*>(QStringLiteral("rawHistogram")));
    if (!dialog.property("rawPreviewAccurate").toBool() || !preview->property("previewScaleReady").toBool() ||
        !histogram->histogram())
      return;
    if (stage == 0) {
      const auto plot = histogram->plot_rect();
      send_mouse(*histogram, QEvent::MouseMove, QPoint(plot.left() + plot.width() / 2, plot.center().y()), Qt::NoButton,
                 Qt::NoButton);
      save_widget_artifact("issue_393_after", dialog);
      auto* tone = dialog.findChild<QSlider*>(QStringLiteral("rawExposureSlider"))->parentWidget();
      save_stacked_artifact("issue_394_after",
                            {tone->grab().toImage(),
                             render_widget_tooltip(*dialog.findChild<QSlider*>(QStringLiteral("rawExposureSlider")))});
      send_mouse(*histogram, QEvent::MouseMove, QPoint(-5, -5), Qt::NoButton, Qt::NoButton);
      dialog.findChild<QPushButton*>(QStringLiteral("rawZoom100Button"))->click();
      stage = 1;
      return;
    }
    save_widget_artifact("issue_395_after", dialog);
    dialog.reject();
    stage = 2;
  }));
  CHECK(stage == 2);
  CHECK(QFileInfo::exists(sidecar) == had_sidecar);
}

// Shift-click on all four Tone sliders against Image > Adjustments > Auto Tone on the default
// develop of a real photo (GitHub issue 394); prints the numbers. Skipped without the fixture.
void ui_raw_auto_tone_matches_image_auto_tone_if_available() {
  using namespace patchy::ui;
  const auto source = patchy::test::source_root_path() / "local-test-fixtures" / "raw" / "sony_fx30_fx300354.arw";
  if (!std::filesystem::exists(source)) {
    patchy::test::skip("optional fixture missing: local-test-fixtures/raw/sony_fx30_fx300354.arw");
  }
  const auto file = raw_test_bytes(to_qstring(source));
  patchy::raw::DevelopSession session(std::vector<std::uint8_t>(file.begin(), file.end()));
  patchy::raw::DevelopOptions draft;
  draft.quality = patchy::raw::DevelopQuality::Draft;
  const patchy::raw::DevelopParams defaults;
  const auto base = session.develop(defaults, draft);
  const auto values = raw_auto_tone(raw_preview_histogram(base.rgb, base.width, base.height),
                                    RawToneModel(defaults.profile, defaults.processing_version));
  CHECK(values.has_value());

  // Auto Tone: each channel's 0.1% clip points stretched to 0 and 255.
  auto auto_toned = base.rgb;
  const auto pixel_count = static_cast<std::uint64_t>(base.width) * static_cast<std::uint64_t>(base.height);
  for (std::size_t channel = 0; channel < 3; ++channel) {
    patchy::AutoLevelsHistogram counts{};
    for (std::size_t index = channel; index < base.rgb.size(); index += 3) ++counts[base.rgb[index]];
    const auto record = patchy::auto_levels_clip_scan(counts, pixel_count);
    const auto lut = record ? patchy::auto_levels_lut(*record) : patchy::auto_levels_identity_lut();
    for (std::size_t index = channel; index < auto_toned.size(); index += 3) auto_toned[index] = lut[auto_toned[index]];
  }

  auto params = defaults;
  params.exposure_ev = values->exposure_ev;
  params.contrast = values->contrast;
  params.highlights = values->highlights;
  params.shadows = values->shadows;
  const auto shifted = session.develop(params, draft);

  struct Stats {
    double mean;
    int black;
    int white;
    std::array<int, 3> percentiles;
  };
  const auto stats_of = [](const std::vector<std::uint8_t>& rgb, int width, int height) {
    const auto histogram = raw_preview_histogram(rgb, width, height);
    std::uint64_t weighted = 0;
    for (std::size_t code = 0; code < 256; ++code) weighted += code * histogram.luminance[code];
    const auto clip = patchy::auto_levels_clip_scan(histogram.luminance, histogram.samples);
    Stats stats{static_cast<double>(weighted) / static_cast<double>(histogram.samples), clip ? clip->black_input : 0,
                clip ? clip->white_input : 255, {}};
    const std::array<double, 3> shares = {0.10, 0.50, 0.90};
    for (std::size_t index = 0; index < shares.size(); ++index) {
      std::uint64_t cumulative = 0;
      int code = 0;
      for (; code < 255; ++code) {
        cumulative += histogram.luminance[static_cast<std::size_t>(code)];
        if (static_cast<double>(cumulative) >= shares[index] * static_cast<double>(histogram.samples)) break;
      }
      stats.percentiles[index] = code;
    }
    return stats;
  };
  const auto print = [](const char* name, const Stats& stats) {
    std::cout << "  " << name << ": mean " << stats.mean << ", clip black/white " << stats.black << "/" << stats.white
              << ", p10/p50/p90 " << stats.percentiles[0] << "/" << stats.percentiles[1] << "/"
              << stats.percentiles[2] << '\n';
  };
  const auto default_stats = stats_of(base.rgb, base.width, base.height);
  const auto auto_tone_stats = stats_of(auto_toned, base.width, base.height);
  const auto shifted_stats = stats_of(shifted.rgb, shifted.width, shifted.height);
  std::cout << "  Shift-click values: Exposure " << values->exposure_ev << " EV, Contrast " << values->contrast
            << ", Highlights " << values->highlights << ", Shadows " << values->shadows << '\n';
  print("default develop", default_stats);
  print("Auto Tone", auto_tone_stats);
  print("Shift-click all four", shifted_stats);
  CHECK(std::abs(shifted_stats.mean - auto_tone_stats.mean) < 25.0);
  CHECK(std::abs(shifted_stats.white - auto_tone_stats.white) <= 25);
  CHECK(std::abs(shifted_stats.percentiles[1] - auto_tone_stats.percentiles[1]) <= 25);

  const auto to_image = [](const std::vector<std::uint8_t>& rgb, int width, int height) {
    QImage image(width, height, QImage::Format_RGB888);
    for (int y = 0; y < height; ++y)
      std::memcpy(image.scanLine(y), rgb.data() + static_cast<std::size_t>(y) * width * 3,
                  static_cast<std::size_t>(width) * 3);
    return image.scaledToWidth(640, Qt::SmoothTransformation);
  };
  save_stacked_artifact("issue_394_auto_tone_parity",
                        {to_image(base.rgb, base.width, base.height), to_image(auto_toned, base.width, base.height),
                         to_image(shifted.rgb, shifted.width, shifted.height)});
}

}  // namespace

std::vector<patchy::test::TestCase> camera_raw_heif_tests() {
  return {
      {"ui_raw_quick_edits_preempt_refinement_and_initial_open_waits", ui_raw_quick_edits_preempt_refinement_and_initial_open_waits},
      {"ui_raw_open_progress_continues_matching_refinement", ui_raw_open_progress_continues_matching_refinement},
      {"ui_raw_legacy_sidecars_preserve_processing_and_upgrade_explicitly", ui_raw_legacy_sidecars_preserve_processing_and_upgrade_explicitly},
      {"ui_raw_sidecar_round_trips_unicode_and_preserves_unknown_fields", ui_raw_sidecar_round_trips_unicode_and_preserves_unknown_fields},
      {"ui_raw_sidecar_rejects_invalid_and_preserves_failed_writes", ui_raw_sidecar_rejects_invalid_and_preserves_failed_writes},
      {"ui_raw_dialog_open_cancel_reset_and_global_isolation", ui_raw_dialog_open_cancel_reset_and_global_isolation},
      {"ui_raw_dialog_preview_matches_open_and_script", ui_raw_dialog_preview_matches_open_and_script},
      {"ui_raw_dialog_auto_controls_and_open_during_refinement", ui_raw_dialog_auto_controls_and_open_during_refinement},
      {"ui_raw_dialog_failed_saves_and_untouched_precision", ui_raw_dialog_failed_saves_and_untouched_precision},
      {"ui_zoomable_preview_downsampling_and_logical_dimensions", ui_zoomable_preview_downsampling_and_logical_dimensions},
      {"ui_raw_local_photo_visual_acceptance_if_available", ui_raw_local_photo_visual_acceptance_if_available},
      {"ui_raw_develop_dialog_accept_opens_document_and_save_routes_to_psd",
       ui_raw_develop_dialog_accept_opens_document_and_save_routes_to_psd},
      {"ui_raw_develop_dialog_cancel_aborts_open", ui_raw_develop_dialog_cancel_aborts_open},
      {"ui_raw_file_drop_returns_before_dialog_and_preserves_batch",
       ui_raw_file_drop_returns_before_dialog_and_preserves_batch},
      {"ui_raw_develop_dialog_exposure_slider_brightens_preview",
       ui_raw_develop_dialog_exposure_slider_brightens_preview},
      {"ui_raw_preference_disabled_opens_with_camera_defaults",
       ui_raw_preference_disabled_opens_with_camera_defaults},
      {"ui_heif_open_is_read_only_if_available", ui_heif_open_is_read_only_if_available},
      {"ui_raw_histogram_regions_and_auto_values", ui_raw_histogram_regions_and_auto_values},
      {"ui_raw_dialog_histogram_drag_and_modifier_clicks", ui_raw_dialog_histogram_drag_and_modifier_clicks},
      {"ui_raw_dialog_zoom_controls_and_scrubby_zoom", ui_raw_dialog_zoom_controls_and_scrubby_zoom},
      {"ui_raw_dialog_issue_shots_if_available", ui_raw_dialog_issue_shots_if_available},
      {"ui_raw_auto_tone_matches_image_auto_tone_if_available", ui_raw_auto_tone_matches_image_auto_tone_if_available},
      {"ui_flat_save_of_layered_document_warns_and_saves_copy",
       ui_flat_save_of_layered_document_warns_and_saves_copy},
  };
}
