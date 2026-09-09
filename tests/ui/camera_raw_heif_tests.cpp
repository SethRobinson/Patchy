#include "ui/canvas_widget.hpp"
#include "core/adjustment_layer.hpp"
#include "core/contour_presets.hpp"
#include "core/gradient_presets.hpp"
#include "core/layer_metadata.hpp"
#include "core/pattern_presets.hpp"
#include "core/smart_filter.hpp"
#include "core/smart_filter_effects.hpp"
#include "core/smart_object.hpp"
#include "core/text_warp.hpp"
#include "ui/smart_object_render.hpp"
#include "core/layer_tree.hpp"
#include "core/palette.hpp"
#include "core/palette_presets.hpp"
#include "ui/palette_panel.hpp"
#include "ui/pattern_library.hpp"
#include "ui/pattern_manager_dialog.hpp"
#include "ui/photo_pattern_presets.hpp"
#include "ui/style_browser.hpp"
#include "ui/style_library.hpp"
#include "ui/style_manager_dialog.hpp"
#include "psd/asl_io.hpp"
#include "psd/psd_binary.hpp"
#include "psd/psd_layer_effects.hpp"
#include "core/style_presets.hpp"
#include "ui/brush_tip_library.hpp"
#include "ui/brush_tip_manager_dialog.hpp"
#include "ui/brush_tip_picker.hpp"
#include "ui/blend_if_range_editor.hpp"
#include "ui/color_panel.hpp"
#include "ui/default_brush_tips.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/document_float_window.hpp"
#include "ui/compatibility_report.hpp"
#include "ui/curves_editor.hpp"
#include "ui/curves_presets.hpp"
#include "ui/filter_workflows.hpp"
#include "ui/filter_look_library.hpp"
#include "ui/font_picker.hpp"
#include "ui/gradient_stops_editor.hpp"
#include "ui/gradient_library.hpp"
#include "ui/gradient_manager_dialog.hpp"
#include "formats/acv_curves_io.hpp"
#include "formats/bmp_document_io.hpp"
#include "formats/aseprite_document_io.hpp"
#include "formats/ico_document_io.hpp"
#include "formats/tga_document_io.hpp"
#include "ui/image_document_io.hpp"
#include "ui/image_save_options_dialog.hpp"
#include "ui/layer_list_widget.hpp"
#include "ui/layer_style_dialog.hpp"
#include "ui/localization.hpp"
#include "ui/main_window.hpp"
#include "ui/print_dialog.hpp"
#include "ui/selection_outline.hpp"
#include "ui/sprite_sheet_dialog.hpp"
#include "ui/splash_dialog.hpp"
#include "ui/app_settings.hpp"
#include "ui/update_checker.hpp"
#include "ui/visual_filter_gallery_dialog.hpp"
#include "ui/zoomable_image_preview.hpp"
#include "ui/raw_develop_dialog.hpp"
#include "ui/raw_develop_settings.hpp"
#include "ui/script_engine.hpp"
#include "ui/qt_paths.hpp"
#include "unicode_path_names.hpp"
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include "ui/zoom_status_bar.hpp"
#include "filters/builtin_filters.hpp"
#include "psd/psd_document_io.hpp"
#include "psd/psd_filter_effects.hpp"
#include "render/compositor.hpp"
#include "synthetic_dng.hpp"
#include "test_fonts.hpp"
#include "test_harness.hpp"
#include "local_psd_fixtures.hpp"

#include <QAbstractItemModel>
#include <QAbstractSpinBox>
#include <QAbstractItemView>
#include <QAbstractTextDocumentLayout>
#include <QAction>
#include <QApplication>
#include <QBuffer>
#include <QByteArray>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDataStream>
#include <QDockWidget>
#include <QDir>
#include <QDoubleSpinBox>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFontComboBox>
#include <QFontDatabase>
#include <QFrame>
#include <QGroupBox>
#include <QImage>
#include <QImageReader>
#include <QImageWriter>
#include <QInputDevice>
#include <QInputDialog>
#include <QKeyEvent>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QListView>
#include <QLayout>
#include <QListWidget>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QLocale>
#include <QSizeGrip>
#include <QMetaObject>
#include <QMouseEvent>
#include <QMenu>
#include <QMenuBar>
#include <QMimeData>
#include <QMessageBox>
#include <QIODevice>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QPolygonF>
#include <QThread>
#include <QPaintEvent>
#include <QPixmap>
#include <QPointingDevice>
#include <QProgressDialog>
#include <QPushButton>
#include <QStackedWidget>
#include <QRadioButton>
#include <QSpinBox>
#include <QStringList>
#include <QScrollBar>
#include <QScreen>
#include <QSettings>
#include <QSlider>
#include <QStandardItemModel>
#include <QStatusBar>
#include <QStyle>
#include <QStyleOptionSlider>
#include <QStyleOptionSpinBox>
#include <QTabBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QTabletEvent>
#include <QTest>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextEdit>
#include <QTextDocument>
#include <QTextFragment>
#include <QTextLayout>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QUrl>
#include <QVariant>
#include <QWheelEvent>
#include <QWindow>
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
      CHECK(elapsed.elapsed() < 30000);
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
  CHECK(!raw_test_dialog(path, [&](QDialog& dialog) {
    auto* done = dialog.findChild<QPushButton*>(QStringLiteral("rawDoneButton"));
    if (!done->isEnabled()) return;
    CHECK(dialog.findChild<QComboBox*>(QStringLiteral("rawProfileCombo"))->currentData().toString() == QStringLiteral("neutral"));
    CHECK(dialog.findChild<QLabel*>(QStringLiteral("rawProcessingNote"))->isVisible());
    CHECK(dialog.findChild<QComboBox*>(QStringLiteral("rawColorNoiseCombo"))->currentData().toInt() == 0);
    CHECK(dialog.findChild<QSlider*>(QStringLiteral("rawDenoiseSlider"))->value() == 182);
    done->click();
  }));
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
  // Version 2 Natural settings also remain recognized and byte-identical on Done.
  params = {};
  params.processing_version = 2;
  CHECK(save_raw_develop_settings(path, params, loaded).isEmpty());
  loaded = load_raw_develop_settings(path);
  CHECK(loaded.recognized && loaded.params.processing_version == 2);
  CHECK(!raw_test_dialog(path, [&](QDialog& dialog) {
    auto* done = dialog.findChild<QPushButton*>(QStringLiteral("rawDoneButton"));
    if (!done->isEnabled()) return;
    CHECK(dialog.findChild<QLabel*>(QStringLiteral("rawProcessingNote"))->isVisible());
    done->click();
  }));
  CHECK(raw_test_bytes(raw_develop_settings_path(path)) == loaded.original_bytes);
}

void ui_raw_dialog_done_cancel_reset_and_global_isolation() {
  using namespace patchy::ui;
  SettingsValueRestorer global(QStringLiteral("imports/rawDevelopExposure"));
  auto settings = app_settings();
  settings.setValue(QStringLiteral("imports/rawDevelopExposure"), 2.0);
  settings.sync();
  const auto a = write_raw_dng_fixture(QStringLiteral("raw_per_photo_a.dng"));
  const auto b = write_raw_dng_fixture(QStringLiteral("raw_per_photo_b.dng"));
  bool changed = false;
  CHECK(!raw_test_dialog(a, [&](QDialog& dialog) {
    auto* exposure = dialog.findChild<QSlider*>(QStringLiteral("rawExposureSlider"));
    auto* done = dialog.findChild<QPushButton*>(QStringLiteral("rawDoneButton"));
    if (!done->isEnabled()) return;
    CHECK(exposure->value() == 0);
    exposure->setValue(125);
    changed = true;
    done->click();
  }));
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
  CHECK(!raw_test_dialog(a, [&](QDialog& dialog) {
    auto* done = dialog.findChild<QPushButton*>(QStringLiteral("rawDoneButton"));
    if (!done->isEnabled()) return;
    dialog.findChild<QPushButton*>(QStringLiteral("rawResetButton"))->click();
    CHECK(dialog.findChild<QSlider*>(QStringLiteral("rawExposureSlider"))->value() == 0);
    done->click();
  }));
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
  auto* tabs = qobject_cast<QTabWidget*>(window.centralWidget());
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
  const auto initial = raw_test_dialog(path, [&](QDialog& dialog) {
    if (clicked) return;
    clicked = true;
    dialog.findChild<QSlider*>(QStringLiteral("rawSaturationSlider"))->setValue(10);
    dialog.findChild<QPushButton*>(QStringLiteral("rawOpenButton"))->click();
  });
  CHECK(clicked && initial.has_value() && initial->params.saturation == 10);
  CHECK(initial->document.width() == 2601 && initial->document.height() == 1703);
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
  for (const bool open : {false, true}) {
    int stage = 0;
    std::exception_ptr prompt_error;
    QTimer prompt_timer;
    QObject::connect(&prompt_timer, &QTimer::timeout, [&] {
      auto* box = qobject_cast<QMessageBox*>(find_top_level_dialog(QStringLiteral("rawSettingsSaveMessageBox")));
      if (!box) return;
      try {
        CHECK(stage == 1);
        stage = 2;
        if (open) box->findChild<QPushButton*>(QStringLiteral("rawOpenWithoutSavingButton"))->click();
        else box->button(QMessageBox::Cancel)->click();
      } catch (...) {
        prompt_error = std::current_exception();
        box->reject();
      }
    });
    prompt_timer.start(10);
    const auto outcome = raw_test_dialog(path, [&](QDialog& dialog) {
      if (stage == 0) {
        if (!dialog.findChild<QPushButton*>(QStringLiteral("rawDoneButton"))->isEnabled()) return;
        dialog.findChild<QSlider*>(QStringLiteral("rawExposureSlider"))->setValue(50);
        // An external directory at the sidecar path deterministically prevents saving.
        CHECK(QDir().mkdir(sidecar_path));
        stage = 1;
        dialog.findChild<QPushButton*>(open ? QStringLiteral("rawOpenButton") : QStringLiteral("rawDoneButton"))->click();
      } else if (stage == 2 && !open) {
        CHECK(dialog.isVisible());
        CHECK(dialog.findChild<QPushButton*>(QStringLiteral("rawDoneButton"))->isEnabled());
        stage = 3;
        dialog.reject();
      }
    });
    prompt_timer.stop();
    CHECK(QDir().rmdir(sidecar_path));
    if (prompt_error) std::rethrow_exception(prompt_error);
    CHECK(outcome.has_value() == open);
    CHECK(stage == (open ? 2 : 3));
  }
  patchy::raw::DevelopParams precise;
  precise.white_balance = patchy::raw::WhiteBalanceMode::Custom;
  precise.custom_white_balance = {4300.123, 12.345};
  precise.exposure_ev = 0.12345;
  CHECK(save_raw_develop_settings(path, precise, {}).isEmpty());
  const auto saved = raw_test_bytes(sidecar_path);
  CHECK(!raw_test_dialog(path, [&](QDialog& dialog) {
    auto* done = dialog.findChild<QPushButton*>(QStringLiteral("rawDoneButton"));
    if (done->isEnabled()) done->click();
  }));
  CHECK(raw_test_bytes(sidecar_path) == saved);
  raw_test_write(sidecar_path, QByteArray("unsupported"));
  CHECK(!raw_test_dialog(path, [&](QDialog& dialog) {
    CHECK(!dialog.findChild<QLabel*>(QStringLiteral("rawSettingsNotice"))->text().isEmpty());
    auto* done = dialog.findChild<QPushButton*>(QStringLiteral("rawDoneButton"));
    if (done->isEnabled()) done->click();
  }));
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
  for (const bool shelves : {false, true}) {
    const auto source = patchy::test::source_root_path() / "local-test-fixtures" / "raw" /
        (shelves ? "sony_fx30_fx300354.arw" : "sony_fx30_fx300416.arw");
    if (!std::filesystem::exists(source)) {
      std::cout << "[SKIP] local Sony FX30 fixture missing\n";
      continue;
    }
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

  // Machines without a platform HEIC decoder (the remote Linux builder's aqt Qt has no
  // kimg_heif; Windows may lack the Store codec packages) raise the open-failed box,
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
                                   error_text.contains(QStringLiteral("Flatpak codec extension"));
    CHECK(codec_unavailable);
    std::cout << "[SKIP] HEIC platform decoder unavailable: " << error_text.toStdString() << '\n';
    return;
  }

  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  CHECK(document.width() == 64);
  CHECK(document.height() == 48);
  CHECK(patchy::ui::MainWindowTestAccess::active_session_path(window) == path);
  CHECK(!patchy::ui::MainWindowTestAccess::active_session_is_modified(window));
  // Same layer name on every platform, whichever decode path ran (WIC or Qt fallback).
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

}  // namespace

std::vector<patchy::test::TestCase> camera_raw_heif_tests() {
  return {
      {"ui_raw_quick_edits_preempt_refinement_and_initial_open_waits", ui_raw_quick_edits_preempt_refinement_and_initial_open_waits},
      {"ui_raw_legacy_sidecars_preserve_processing_and_upgrade_explicitly", ui_raw_legacy_sidecars_preserve_processing_and_upgrade_explicitly},
      {"ui_raw_sidecar_round_trips_unicode_and_preserves_unknown_fields", ui_raw_sidecar_round_trips_unicode_and_preserves_unknown_fields},
      {"ui_raw_sidecar_rejects_invalid_and_preserves_failed_writes", ui_raw_sidecar_rejects_invalid_and_preserves_failed_writes},
      {"ui_raw_dialog_done_cancel_reset_and_global_isolation", ui_raw_dialog_done_cancel_reset_and_global_isolation},
      {"ui_raw_dialog_preview_matches_open_and_script", ui_raw_dialog_preview_matches_open_and_script},
      {"ui_raw_dialog_auto_controls_and_open_during_refinement", ui_raw_dialog_auto_controls_and_open_during_refinement},
      {"ui_raw_dialog_failed_saves_and_untouched_precision", ui_raw_dialog_failed_saves_and_untouched_precision},
      {"ui_zoomable_preview_downsampling_and_logical_dimensions", ui_zoomable_preview_downsampling_and_logical_dimensions},
      {"ui_raw_local_photo_visual_acceptance_if_available", ui_raw_local_photo_visual_acceptance_if_available},
      {"ui_raw_develop_dialog_accept_opens_document_and_save_routes_to_psd",
       ui_raw_develop_dialog_accept_opens_document_and_save_routes_to_psd},
      {"ui_raw_develop_dialog_cancel_aborts_open", ui_raw_develop_dialog_cancel_aborts_open},
      {"ui_raw_develop_dialog_exposure_slider_brightens_preview",
       ui_raw_develop_dialog_exposure_slider_brightens_preview},
      {"ui_raw_preference_disabled_opens_with_camera_defaults",
       ui_raw_preference_disabled_opens_with_camera_defaults},
      {"ui_heif_open_is_read_only_if_available", ui_heif_open_is_read_only_if_available},
      {"ui_flat_save_of_layered_document_warns_and_saves_copy",
       ui_flat_save_of_layered_document_warns_and_saves_copy},
  };
}
