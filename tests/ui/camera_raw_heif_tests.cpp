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
  const auto bytes = patchy::test::synthetic_bayer_dng(128, 96);
  QFile file(path);
  CHECK(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
  CHECK(file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<qsizetype>(bytes.size())) ==
        static_cast<qsizetype>(bytes.size()));
  return path;
}

// The develop dialog persists its parameters on accept (last-used-as-defaults), so raw
// UI tests must not inherit each other's (or the user's) imports/rawDevelop* values.
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
        // Wait for the first develop: previews always run at half size (64x48). The
        // sanitized defaults have auto-brighten off, so the exposure shift below is
        // monotonic with no extra setup.
        if (image.isNull() || image.width() != 64 || !status->text().isEmpty()) {
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
