#include "local_psd_fixtures.hpp"
#include <QByteArray>
#include <QIODevice>
#include <QUrl>
#include <algorithm>
#include <atomic>
#include <array>
#include <cstring>
#include <exception>
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
#include <vector>
// Automatic selection in the app (docs/ai-selection.md): the Object Selection tool, Select >
// Subject and the script calls. Model results are CPU-dependent float math, so checks look
// at where the selection lands, never at exact pixels. Builds without the models skip.
#include "ui/canvas_widget.hpp"
#include "ui/main_window.hpp"
#include "ui/main_window_shared.hpp"
#include "ui/script_engine.hpp"
#include "ui/app_settings.hpp"
#include "ui/model_download_dialog.hpp"
#include "ui/object_selection_model_download.hpp"
#include "ui/watermark_model_download.hpp"
#include "ui/qt_paths.hpp"
#include "ai/segmentation_engine.hpp"
#include "core/pixel_depth.hpp"
#include "test_harness.hpp"
#include "ui_test_access.hpp"
#include "ui_test_support.hpp"
#include "unicode_path_names.hpp"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QStringList>
#include <QPushButton>
#include <QTimer>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <utility>

namespace {

using namespace patchy::test::ui;

// The High Quality model's download folder is the user's app data, which these tests do not
// isolate: pin it to an empty build-local folder so they always run the bundled Standard
// model unless a test opts into the staged copy (ui_object_select_model_option_and_zoomed_box).
void pin_downloaded_models_to_nothing() {
  patchy::ai::set_downloaded_model_directory(std::filesystem::current_path() / "ai-models-none");
}

void automatic_selection_or_skip() {
  pin_downloaded_models_to_nothing();
  patchy::ui::configure_automatic_selection_models();
  if (!patchy::ai::segmentation_available()) {
    patchy::test::skip("build without automatic selection");
  }
}

// Select Subject, Select Background and Remove Background need the subject model, which
// builds leave out while PATCHY_ENABLE_SELECT_SUBJECT is off (docs/ai-selection.md).
void subject_selection_or_skip() {
  pin_downloaded_models_to_nothing();
  patchy::ui::configure_automatic_selection_models();
  if (!patchy::ai::subject_selection_available()) {
    patchy::test::skip("build without the Select Subject model");
  }
}

// A self-authored scene: a shaded red disc (the one object) on mottled green noise.
patchy::Document disc_document(int width, int height, QPoint center, int radius) {
  patchy::Document document(width, height, patchy::PixelFormat::rgba8());
  patchy::PixelBuffer pixels(width, height, patchy::PixelFormat::rgba8());
  std::uint64_t state = 11;
  const auto next = [&state] {
    std::uint64_t z = (state += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30U)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27U)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31U);
  };
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      auto* px = pixels.pixel(x, y);
      const int noise = static_cast<int>(next() % 25U);
      const double dx = x + 0.5 - center.x();
      const double dy = y + 0.5 - center.y();
      if (dx * dx + dy * dy <= static_cast<double>(radius) * radius) {
        const double shade = 0.65 + 0.35 * std::max(0.0, -(dx + dy) / (radius * 1.5));
        px[0] = static_cast<std::uint8_t>(200.0 * shade);
        px[1] = static_cast<std::uint8_t>(40.0 * shade);
        px[2] = static_cast<std::uint8_t>(35.0 * shade);
      } else {
        const int blotch = static_cast<int>(18.0 * std::sin(x * 0.05) * std::cos(y * 0.07));
        px[0] = static_cast<std::uint8_t>(std::clamp(95 + noise + blotch, 0, 255));
        px[1] = static_cast<std::uint8_t>(std::clamp(120 + noise + blotch, 0, 255));
        px[2] = static_cast<std::uint8_t>(std::clamp(90 + noise, 0, 255));
      }
      px[3] = 255;
    }
  }
  document.add_pixel_layer("Scene", std::move(pixels));
  return document;
}

void ui_object_select_box_and_clicks_select_on_release() {
  automatic_selection_or_skip();
  const QPoint center(170, 130);
  const int radius = 60;
  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(disc_document(360, 260, center, radius), QStringLiteral("Object Selection"));
  QApplication::processEvents();
  auto* canvas = require_canvas(window);
  require_action(window, "toolObjectSelectionAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->tool() == patchy::ui::CanvasTool::ObjectSelect);
  canvas->set_selection_feather_radius(0);
  auto* undo_action = require_action_by_text(window, QStringLiteral("Undo"));
  const auto widget_point = [canvas](QPoint document_point) {
    return canvas->widget_position_for_document_point(document_point);
  };

  // A box drag: nothing is selected until release (the patent boundary), then the disc.
  send_mouse(*canvas, QEvent::MouseButtonPress, widget_point(QPoint(95, 55)), Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseMove, widget_point(QPoint(180, 150)), Qt::NoButton, Qt::LeftButton);
  CHECK(!canvas->has_selection());
  send_mouse(*canvas, QEvent::MouseMove, widget_point(QPoint(245, 205)), Qt::NoButton, Qt::LeftButton);
  CHECK(!canvas->has_selection());
  save_widget_artifact("ui_object_select_box_drag", *canvas);
  send_mouse(*canvas, QEvent::MouseButtonRelease, widget_point(QPoint(245, 205)), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(canvas->has_selection());
  auto region = canvas->selected_document_region();
  CHECK(region.contains(center));
  CHECK(region.contains(center + QPoint(radius - 6, 0)));
  CHECK(!region.contains(center + QPoint(radius + 8, 0)));
  CHECK(!region.contains(QPoint(10, 10)));
  CHECK(!region.contains(QPoint(100, 60)));
  CHECK(canvas->object_select_encodes_started() == 1);
  save_widget_artifact("ui_object_select_box_selection", *canvas);

  undo_action->trigger();
  QApplication::processEvents();
  CHECK(!canvas->has_selection());

  // A click reuses the embedding (no second encode) and selects the disc under it.
  send_mouse(*canvas, QEvent::MouseButtonPress, widget_point(center), Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, widget_point(center), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(canvas->object_select_encodes_started() == 1);
  CHECK(canvas->has_selection());
  region = canvas->selected_document_region();
  CHECK(region.contains(center + QPoint(0, radius - 6)));
  CHECK(!region.contains(QPoint(330, 230)));

  // Alt+click subtracts by plain boolean: the disc leaves the selection.
  send_mouse(*canvas, QEvent::MouseButtonPress, widget_point(center), Qt::LeftButton, Qt::LeftButton, Qt::AltModifier);
  send_mouse(*canvas, QEvent::MouseButtonRelease, widget_point(center), Qt::LeftButton, Qt::NoButton, Qt::AltModifier);
  QApplication::processEvents();
  CHECK(!canvas->selected_document_region().contains(center));

  // A change notification alone keeps the embedding (the pixels hash the same)...
  canvas->document_changed();
  send_mouse(*canvas, QEvent::MouseButtonPress, widget_point(center), Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, widget_point(center), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(canvas->object_select_encodes_started() == 1);
  // ...and an actual pixel edit makes the next press encode again.
  {
    auto& host = window.script_engine_host();
    patchy::ui::ScriptEngineHost::RunOptions edit_options;
    edit_options.name = QStringLiteral("edit-pixels");
    (void)host.run_source(QStringLiteral("app.activeDocument.activeLayer.fillRect(0, 0, 12, 12, '#ffffff');"),
                          std::move(edit_options));
    QElapsedTimer edit_timer;
    edit_timer.start();
    while (host.run_active() && edit_timer.elapsed() < 15000) {
      QApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 20);
    }
    QApplication::processEvents();
    CHECK(!host.last_run_had_error());
  }
  send_mouse(*canvas, QEvent::MouseButtonPress, widget_point(center), Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, widget_point(center), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(canvas->object_select_encodes_started() == 2);
}

void ui_object_select_tool_and_options_are_listed() {
  automatic_selection_or_skip();
  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(disc_document(64, 64, QPoint(32, 32), 12), QStringLiteral("Options"));
  QApplication::processEvents();
  require_action(window, "toolObjectSelectionAction")->trigger();
  QApplication::processEvents();
  CHECK(require_canvas(window)->tool() == patchy::ui::CanvasTool::ObjectSelect);
  auto* sample_all = window.findChild<QCheckBox*>(QStringLiteral("objectSelectSampleAllLayersCheck"));
  auto* subject_button = window.findChild<QPushButton*>(QStringLiteral("selectSubjectButton"));
  CHECK(sample_all != nullptr && sample_all->isVisibleTo(&window));
  const bool subject = patchy::ai::subject_selection_available();
  CHECK(subject_button != nullptr && subject_button->isVisibleTo(&window) == subject);
  sample_all->setChecked(false);
  CHECK(!require_canvas(window)->object_select_sample_all_layers());
  sample_all->setChecked(true);
  auto* subject_action = require_action(window, "selectSubjectAction");
  CHECK(subject_action->isVisible() == subject);
}

// The options bar's Model choice, and zoom-to-box: a box much smaller than the image encodes
// its own region on release (one extra encode) and still selects just the object.
void ui_object_select_model_option_and_zoomed_box() {
  automatic_selection_or_skip();
  const QPoint center(700, 520);
  const int radius = 90;
  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(disc_document(1400, 1000, center, radius), QStringLiteral("Zoom"));
  QApplication::processEvents();
  auto* canvas = require_canvas(window);
  require_action(window, "toolObjectSelectionAction")->trigger();
  QApplication::processEvents();
  canvas->set_selection_feather_radius(0);
  auto* combo = window.findChild<QComboBox*>(QStringLiteral("objectSelectModelCombo"));
  CHECK(combo != nullptr && combo->count() == 2 && combo->isVisibleTo(&window));
  CHECK(combo != nullptr && combo->currentIndex() == 0);  // nothing downloaded: Standard
  const auto widget_point = [canvas](QPoint document_point) {
    return canvas->widget_position_for_document_point(document_point);
  };

  send_mouse(*canvas, QEvent::MouseButtonPress, widget_point(QPoint(580, 400)), Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseMove, widget_point(QPoint(820, 640)), Qt::NoButton, Qt::LeftButton);
  CHECK(!canvas->has_selection());
  send_mouse(*canvas, QEvent::MouseButtonRelease, widget_point(QPoint(820, 640)), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  // The press started the whole-image encode; the release encoded the box's region.
  CHECK(canvas->object_select_encodes_started() == 2);
  auto region = canvas->selected_document_region();
  CHECK(region.contains(center));
  CHECK(region.contains(center + QPoint(radius - 6, 0)));
  CHECK(!region.contains(center + QPoint(radius + 10, 0)));
  CHECK(!region.contains(QPoint(590, 410)));

  // With the High Quality model present (the build's stand-in for the download folder), the
  // Model choice switches to it and the next press encodes with it.
  patchy::ai::set_downloaded_model_directory(std::filesystem::current_path() / "ai-models-download");
  if (!patchy::ai::high_quality_model_available()) {
    std::printf("  (High Quality model not staged; the model switch is not exercised)\n");
  } else if (combo != nullptr) {
    combo->setCurrentIndex(1);
    emit combo->activated(1);
    QApplication::processEvents();
    CHECK(combo->currentIndex() == 1);
    CHECK(patchy::ai::active_sam_model() == patchy::ai::SamModel::HighQuality);
    send_mouse(*canvas, QEvent::MouseButtonPress, widget_point(center), Qt::LeftButton, Qt::LeftButton);
    send_mouse(*canvas, QEvent::MouseButtonRelease, widget_point(center), Qt::LeftButton, Qt::NoButton);
    QApplication::processEvents();
    CHECK(canvas->object_select_encodes_started() == 3);
    region = canvas->selected_document_region();
    CHECK(region.contains(center) && !region.contains(QPoint(100, 100)));
    combo->setCurrentIndex(0);
    emit combo->activated(0);
    QApplication::processEvents();
    CHECK(combo->currentIndex() == 0);
    CHECK(patchy::ai::active_sam_model() == patchy::ai::SamModel::Standard);
  }
  patchy::ui::set_object_selection_high_quality_setting(true);
  pin_downloaded_models_to_nothing();
}

// The High Quality offer: "Not Now" downloads nothing and keeps Standard; with the model
// already present, the download call returns at once without any dialog.
void ui_object_selection_model_offer_not_now_keeps_standard() {
  automatic_selection_or_skip();
  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(disc_document(200, 160, QPoint(100, 80), 30), QStringLiteral("Offer"));
  QApplication::processEvents();
  require_action(window, "toolObjectSelectionAction")->trigger();
  QApplication::processEvents();  // offscreen: the first-use offer stays away by itself
  CHECK(find_top_level_dialog(QStringLiteral("objectSelectionModelOfferMessageBox")) == nullptr);

  bool saw_offer = false;
  QTimer::singleShot(0, [&saw_offer] {
    auto* box = qobject_cast<QMessageBox*>(find_top_level_dialog(QStringLiteral("objectSelectionModelOfferMessageBox")));
    CHECK(box != nullptr);
    if (box == nullptr) {
      return;
    }
    CHECK(box->informativeText().contains(QStringLiteral("234 MB")));
    for (auto* button : box->buttons()) {
      if (box->buttonRole(button) == QMessageBox::RejectRole) {
        saw_offer = true;
        button->click();
        return;
      }
    }
  });
  CHECK(!patchy::ui::MainWindowTestAccess::offer_high_quality_object_selection_model(window));
  CHECK(saw_offer);
  CHECK(patchy::ai::active_sam_model() == patchy::ai::SamModel::Standard);
  auto* combo = window.findChild<QComboBox*>(QStringLiteral("objectSelectModelCombo"));
  CHECK(combo != nullptr && combo->currentIndex() == 0);

  patchy::ai::set_downloaded_model_directory(std::filesystem::current_path() / "ai-models-download");
  if (patchy::ai::high_quality_model_available()) {
    QString error;
    CHECK(window.download_high_quality_object_selection_model(true, {}, &error));
    CHECK(error.isEmpty());
  }
  pin_downloaded_models_to_nothing();
}

// Preferences > Tools shows the downloaded High Quality model and deletes exactly its two
// files by name: anything else in the folder stays (and so does the folder); with nothing
// else left, the folder goes too. A scratch Unicode folder stands in for the app-data one.
void ui_preferences_deletes_only_the_downloaded_model_files() {
  automatic_selection_or_skip();
  const auto root = patchy::ui::to_qstring(std::filesystem::absolute(
      patchy::test::unicode_artifact_dir(patchy::test::kUnicodeCombinedStem) / "ai-models"));
  QDir().mkpath(root);
  const auto write_models = [&root] {
    for (const auto& file : patchy::ui::high_quality_model_files()) {
      QFile model(QDir(root).filePath(file.name));
      CHECK(model.open(QIODevice::WriteOnly) && model.write("stand-in") == 8);
    }
  };
  write_models();
  const auto keep_path = QDir(root).filePath(QStringLiteral("keep.txt"));
  {
    QFile keep(keep_path);
    CHECK(keep.open(QIODevice::WriteOnly) && keep.write("mine") == 4);
  }
  patchy::ai::set_downloaded_model_directory(patchy::ui::to_filesystem_path(root));

  patchy::ui::MainWindow window;
  show_window(window);
  bool saw_preferences = false;
  // The first-use offer was already made and the user had picked Standard: Delete resets both.
  patchy::ui::app_settings().setValue(QStringLiteral("automaticSelection/highQualityModelOffered"), true);
  patchy::ui::set_object_selection_high_quality_setting(false);
  QTimer::singleShot(0, [&saw_preferences] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyPreferencesDialog"));
    CHECK(dialog != nullptr);
    if (dialog == nullptr) {
      return;
    }
    auto* status = dialog->findChild<QLabel*>(QStringLiteral("preferencesObjectSelectionModelStatus"));
    auto* button = dialog->findChild<QPushButton*>(QStringLiteral("preferencesDeleteObjectSelectionModelButton"));
    auto* download =
        dialog->findChild<QPushButton*>(QStringLiteral("preferencesDownloadObjectSelectionModelButton"));
    CHECK(status != nullptr && status->text().contains(QStringLiteral("downloaded (234 MB)")));
    CHECK(button != nullptr && button->isEnabled());
    CHECK(download != nullptr && !download->isEnabled());
    if (status != nullptr && button != nullptr && download != nullptr) {
      button->click();
      CHECK(status->text().contains(QStringLiteral("not downloaded")));
      CHECK(!button->isEnabled());
      CHECK(download->isEnabled());  // not clicked: it would reach the network
      saw_preferences = true;
    }
    dialog->reject();
  });
  patchy::ui::MainWindowTestAccess::show_preferences(window);
  CHECK(saw_preferences);
  CHECK(!patchy::ui::app_settings().contains(QStringLiteral("automaticSelection/highQualityModelOffered")));
  CHECK(patchy::ui::object_selection_high_quality_setting());
  for (const auto& file : patchy::ui::high_quality_model_files()) {
    CHECK(!QFile::exists(QDir(root).filePath(file.name)));
  }
  CHECK(QFile::exists(keep_path));
  CHECK(!patchy::ai::high_quality_model_available());

  // Nothing else left: the folder goes as well (a plain rmdir, never a recursive delete).
  CHECK(QFile::remove(keep_path));
  write_models();
  QString error;
  CHECK(patchy::ui::delete_downloaded_high_quality_model(root, &error));
  CHECK(error.isEmpty());
  CHECK(!QDir(root).exists());

  pin_downloaded_models_to_nothing();
  if (QDir(root).exists()) {
    remove_test_scratch_dir(root);
  }
}

// The download pins every file by size and SHA-256 and writes through QSaveFile: matching
// files land, a mismatch leaves nothing behind, and present files are skipped. Served from
// file:// URLs into a Unicode folder (every new file-writing entry point gets one).
void ui_object_selection_model_download_verifies_files() {
  const auto root = patchy::ui::to_qstring(
      std::filesystem::absolute(patchy::test::unicode_artifact_dir(patchy::test::kUnicodeCombinedStem)));
  QDir(root).mkpath(QStringLiteral("source"));
  const auto write = [&root](const QString& name, const QByteArray& bytes) {
    QFile file(QDir(root).filePath(QStringLiteral("source/") + name));
    CHECK(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size());
    return QUrl::fromLocalFile(file.fileName());
  };
  const QByteArray first(300000, 'a');
  QByteArray second;
  for (int i = 0; i < 70000; ++i) {
    second.append(static_cast<char>(i % 251));
  }
  const auto sha = [](const QByteArray& bytes) {
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex();
  };
  std::vector<patchy::ui::DownloadableModelFile> files{
      {QStringLiteral("first.onnx"), write(QStringLiteral("first.onnx"), first), first.size(), sha(first)},
      {QStringLiteral("second.onnx"), write(QStringLiteral("second.onnx"), second), second.size(), sha(second)},
  };
  const auto target = QDir(root).filePath(QStringLiteral("models"));
  QObject owner;
  std::vector<patchy::ui::ModelDownloadProgress> reports;
  const auto run = [&owner, &target, &reports](std::vector<patchy::ui::DownloadableModelFile> list,
                                               qint64* last_received) {
    patchy::ui::ModelDownloadResult result;
    bool done = false;
    QEventLoop loop;
    QTimer::singleShot(30000, &loop, &QEventLoop::quit);
    reports.clear();
    auto job = patchy::ui::start_model_download(
        &owner, target, std::move(list),
        [last_received, &reports](const patchy::ui::ModelDownloadProgress& progress) {
          reports.push_back(progress);
          if (last_received != nullptr) {
            *last_received = progress.received;
          }
        },
        [&](patchy::ui::ModelDownloadResult finished) {
          result = std::move(finished);
          done = true;
          loop.quit();
        });
    if (!done) {
      loop.exec();
    }
    CHECK(done);
    return result;
  };

  qint64 received = 0;
  auto result = run(files, &received);
  CHECK(result.ok && !result.cancelled);
  CHECK(received == first.size() + second.size());
  // Each file is announced with its link before any byte arrives, then numbered and sized.
  CHECK(!reports.empty() && reports.front().file_number == 1 && reports.front().file_count == 2 &&
        reports.front().file_received == 0 && reports.front().url == files[0].url &&
        reports.front().file_name == QStringLiteral("first.onnx") && reports.front().file_size == first.size());
  CHECK(std::any_of(reports.begin(), reports.end(), [&](const patchy::ui::ModelDownloadProgress& report) {
    return report.file_number == 2 && report.file_received == 0 && report.url == files[1].url;
  }));
  QFile landed(QDir(target).filePath(QStringLiteral("second.onnx")));
  CHECK(landed.open(QIODevice::ReadOnly) && landed.readAll() == second);
  landed.close();

  // Already present and matching: skipped, still a success.
  result = run(files, nullptr);
  CHECK(result.ok);

  // A checksum mismatch fails and leaves no file (not even a partial one).
  auto wrong = files;
  wrong[0].name = QStringLiteral("wrong.onnx");
  wrong[0].sha256_hex = sha("something else");
  wrong.resize(1);
  result = run(wrong, nullptr);
  CHECK(!result.ok && !result.cancelled && result.error.contains(QStringLiteral("checksum")));
  CHECK(!QFile::exists(QDir(target).filePath(QStringLiteral("wrong.onnx"))));
  CHECK(QDir(target).entryList(QDir::Files).size() == 2);

  // A link that fails moves on to the file's fallback link, which the progress names.
  auto fallback = files;
  fallback.resize(1);
  fallback[0].name = QStringLiteral("third.onnx");
  const auto good = fallback[0].url;
  fallback[0].url = QUrl::fromLocalFile(QDir(root).filePath(QStringLiteral("source/missing.onnx")));
  fallback[0].fallback_urls = {good};
  result = run(fallback, nullptr);
  CHECK(result.ok);
  CHECK(QFile::exists(QDir(target).filePath(QStringLiteral("third.onnx"))));
  CHECK(std::any_of(reports.begin(), reports.end(),
                    [&](const patchy::ui::ModelDownloadProgress& report) { return report.url == fallback[0].url; }));
  CHECK(std::any_of(reports.begin(), reports.end(), [&](const patchy::ui::ModelDownloadProgress& report) {
    return report.url == good && report.file_received == fallback[0].size;
  }));
  // Every link failing reports the last failure and keeps nothing.
  auto broken = fallback;
  broken[0].name = QStringLiteral("fourth.onnx");
  broken[0].fallback_urls = {QUrl::fromLocalFile(QDir(root).filePath(QStringLiteral("source/also-missing.onnx")))};
  result = run(broken, nullptr);
  CHECK(!result.ok && !result.cancelled && !result.error.isEmpty());
  CHECK(!QFile::exists(QDir(target).filePath(QStringLiteral("fourth.onnx"))));

  remove_test_scratch_dir(root);
}

// The download dialog every model download and the one-click update use: the heading, file
// N of M with its size, and the full link (selectable when it fits, elided in the middle with
// the full link in the tooltip and Copy Link when it does not), driven by a real transfer
// from file:// links; the offer texts name the source release.
void ui_model_download_dialog_shows_the_link() {
  CHECK(patchy::ui::model_download_source_text(patchy::ui::watermark_model_files()) ==
        QStringLiteral("github.com/SethRobinson/Patchy (release ai-models-3)"));
  CHECK(patchy::ui::model_download_source_text(patchy::ui::high_quality_model_files()) ==
        QStringLiteral("github.com/SethRobinson/Patchy (release ai-models-2)"));
  for (const auto& file : patchy::ui::watermark_model_files()) {
    CHECK(file.url.toString().startsWith(
        QStringLiteral("https://github.com/SethRobinson/Patchy/releases/download/ai-models-3/")));
    CHECK(file.fallback_urls.size() == 1);  // Adobe's own host, used only when the release fails
  }

  QWidget parent;
  parent.resize(800, 500);
  parent.show();
  QApplication::processEvents();
  patchy::ui::ModelDownloadDialog dialog(&parent);
  dialog.setObjectName(QStringLiteral("watermarkModelDownloadDialog"));
  dialog.setWindowTitle(QStringLiteral("Watermark Models"));
  dialog.set_heading(QStringLiteral("Downloading the watermark models... 52 of 135 MB"));
  dialog.show();
  QApplication::processEvents();
  const auto& files = patchy::ui::watermark_model_files();
  patchy::ui::ModelDownloadProgress progress;
  progress.received = files[0].size + files[1].size / 10;
  progress.total = patchy::ui::watermark_model_download_bytes();
  progress.file_number = 2;
  progress.file_count = static_cast<int>(files.size());
  progress.file_name = files[1].name;
  progress.file_size = files[1].size;
  progress.file_received = files[1].size / 10;
  progress.url = files[1].url;
  dialog.set_progress(progress);
  QApplication::processEvents();
  auto* link = dialog.link_label();
  CHECK(link != nullptr);
  CHECK(dialog.file_text() == QStringLiteral("File 2 of 5: trustmark_decoder_P.onnx (47 MB)"));
  CHECK(link->link() == files[1].url.toString() && link->toolTip() == files[1].url.toString());
  CHECK(!link->elided() && link->text() == files[1].url.toString());
  CHECK(link->textInteractionFlags().testFlag(Qt::TextSelectableByMouse));
  auto* copy = dialog.findChild<QPushButton*>(QStringLiteral("modelDownloadCopyLinkButton"));
  CHECK(copy != nullptr && copy->isEnabled());
  if (copy != nullptr) {
    QApplication::clipboard()->clear();
    copy->click();
    CHECK(QApplication::clipboard()->text() == files[1].url.toString());
  }
  capture_widget_artifact("ui_model_download_dialog", dialog);

  // A link too long to fit: elided in the middle, not selectable, still copied whole.
  const auto long_link = QStringLiteral("https://github.com/SethRobinson/Patchy/releases/download/ai-models-3/") +
                         QString(120, QLatin1Char('x')) + QStringLiteral(".onnx");
  progress.url = QUrl(long_link);
  dialog.set_progress(progress);
  QApplication::processEvents();
  CHECK(link->elided() && link->text() != long_link && link->text().contains(QChar(0x2026)));
  CHECK(link->text().startsWith(QStringLiteral("https://")) && link->text().endsWith(QStringLiteral(".onnx")));
  CHECK(link->toolTip() == long_link);
  CHECK(!link->textInteractionFlags().testFlag(Qt::TextSelectableByMouse));
  if (copy != nullptr) {
    copy->click();
    CHECK(QApplication::clipboard()->text() == long_link);
  }

  // Wired as the app wires it, through a real transfer: the dialog ends on the last file.
  const auto root = patchy::ui::to_qstring(
      std::filesystem::absolute(patchy::test::unicode_artifact_dir(patchy::test::kUnicodeCombinedStem)));
  QDir(root).mkpath(QStringLiteral("dialog-source"));
  const auto source = [&root](const QString& name, int bytes) {
    QFile file(QDir(root).filePath(QStringLiteral("dialog-source/") + name));
    const QByteArray data(bytes, 'z');
    CHECK(file.open(QIODevice::WriteOnly) && file.write(data) == data.size());
    return patchy::ui::DownloadableModelFile{name, QUrl::fromLocalFile(file.fileName()), data.size(),
                                             QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex(), {}};
  };
  std::vector<patchy::ui::DownloadableModelFile> list{source(QStringLiteral("one.onnx"), 4000),
                                                      source(QStringLiteral("two.onnx"), 9000)};
  const auto target = QDir(root).filePath(QStringLiteral("dialog-models"));
  bool done = false;
  bool ok = false;
  QEventLoop loop;
  QTimer::singleShot(30000, &loop, &QEventLoop::quit);
  auto job = patchy::ui::start_model_download(
      &dialog, target, list, [&dialog](const patchy::ui::ModelDownloadProgress& report) { dialog.set_progress(report); },
      [&](patchy::ui::ModelDownloadResult result) {
        ok = result.ok;
        done = true;
        loop.quit();
      });
  QObject::connect(&dialog, &QDialog::rejected, &dialog, [&job] { job->cancel(); });
  if (!done) {
    loop.exec();
  }
  CHECK(done && ok);
  CHECK(dialog.file_text().startsWith(QStringLiteral("File 2 of 2: two.onnx")));
  CHECK(link->link() == QDir::toNativeSeparators(list[1].url.toLocalFile()) || link->link() == list[1].url.toLocalFile());
  dialog.close();
  remove_test_scratch_dir(root);
}

void ui_select_subject_menu_selects_the_subject() {
  subject_selection_or_skip();
  const QPoint center(150, 120);
  const int radius = 55;
  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(disc_document(300, 240, center, radius), QStringLiteral("Select Subject"));
  QApplication::processEvents();
  auto* canvas = require_canvas(window);
  auto* subject_action = require_action(window, "selectSubjectAction");
  subject_action->trigger();
  QApplication::processEvents();
  CHECK(canvas->has_selection());
  const auto region = canvas->selected_document_region();
  CHECK(region.contains(center));
  CHECK(!region.contains(QPoint(8, 8)));
  CHECK(!region.contains(QPoint(290, 230)));
  save_widget_artifact("ui_select_subject_selection", *canvas);
  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  CHECK(!canvas->has_selection());
}

void ui_script_automatic_selection_selects_objects() {
  automatic_selection_or_skip();
  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(disc_document(300, 240, QPoint(150, 120), 55), QStringLiteral("Script"));
  QApplication::processEvents();
  auto& host = window.script_engine_host();
  patchy::ui::ScriptEngineHost::RunOptions options;
  options.name = QStringLiteral("automatic-selection");
  (void)host.run_source(QStringLiteral(R"JS(
    var d = app.activeDocument;
    if (!d.selection.automaticSelectionAvailable) throw Error('unavailable');
    d.selection.objectSelect({rect: {x: 80, y: 50, width: 140, height: 140}});
    var b = d.selection.bounds;
    if (!b || b.x > 100 || b.x + b.width < 200) throw Error('box ' + JSON.stringify(b));
    d.selection.deselect();
    // Hard masks so subtracting the same object empties the selection exactly (soft edges
    // would correctly leave a faint ring: subtract is a * (1 - b)).
    d.selection.objectSelect({point: {x: 150, y: 120}, antialias: false});
    if (!d.selection.exists) throw Error('point');
    d.selection.objectSelect({point: {x: 150, y: 120}, operation: 'subtract', antialias: false});
    if (d.selection.exists) throw Error('subtract ' + JSON.stringify(d.selection.bounds));
    var threw = false;
    if (d.selection.subjectSelectionAvailable) {
      d.selection.selectSubject();
      b = d.selection.bounds;
      if (!b || b.x > 105 || b.y > 75 || b.x + b.width < 195) throw Error('subject ' + JSON.stringify(b));
    } else {
      try { d.selection.selectSubject(); } catch (e) { threw = String(e).indexOf('not included') >= 0; }
      if (!threw) throw Error('selectSubject did not report the missing model');
      threw = false;
    }
    try { d.selection.objectSelect({rect: {x: 0, y: 0, width: 5, height: 5}, point: {x: 1, y: 1}}); }
    catch (e) { threw = true; }
    if (!threw) throw Error('box plus point accepted');
    threw = false;
    try { d.selection.objectSelect({point: {x: 1, y: 1}, operation: 'intersect'}); } catch (e) { threw = true; }
    if (!threw) throw Error('intersect accepted');
  )JS"), std::move(options));
  QElapsedTimer timer;
  timer.start();
  while (host.run_active() && timer.elapsed() < 120000) {
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 20);
  }
  QApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 20);
  CHECK(!host.run_active());
  if (host.last_run_had_error()) {
    for (const auto& line : host.message_backlog()) {
      std::printf("  %s\n", line.toUtf8().constData());
    }
  }
  CHECK(!host.last_run_had_error());
}

void ui_select_background_selects_everything_but_the_subject() {
  subject_selection_or_skip();
  const QPoint center(150, 120);
  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(disc_document(300, 240, center, 55), QStringLiteral("Select Background"));
  QApplication::processEvents();
  auto* canvas = require_canvas(window);
  require_action(window, "selectBackgroundAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->has_selection());
  const auto region = canvas->selected_document_region();
  CHECK(!region.contains(center));
  CHECK(region.contains(QPoint(5, 5)));
  CHECK(region.contains(QPoint(294, 234)));
  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  CHECK(!canvas->has_selection());
}

void ui_remove_background_masks_the_layer_in_one_undo_step() {
  subject_selection_or_skip();
  const QPoint center(150, 120);
  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(disc_document(300, 240, center, 55), QStringLiteral("Remove Background"));
  QApplication::processEvents();
  auto* canvas = require_canvas(window);
  require_action(window, "layerRemoveBackgroundAction")->trigger();
  QApplication::processEvents();
  CHECK(!canvas->has_selection());  // the selection is untouched
  const auto& layer = std::as_const(patchy::ui::MainWindowTestAccess::document(window)).layers().front();
  CHECK(layer.mask().has_value());
  const auto& mask = *layer.mask();
  const auto mask_value = [&mask](QPoint point) {
    if (!mask.bounds.contains(point.x(), point.y())) {
      return static_cast<float>(mask.default_color) / 255.0F;
    }
    return patchy::coverage_at(mask.pixels, point.x() - mask.bounds.x, point.y() - mask.bounds.y);
  };
  CHECK(mask_value(center) > 0.9F);
  CHECK(mask_value(QPoint(5, 5)) < 0.1F);
  CHECK(mask_value(QPoint(294, 234)) < 0.1F);
  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  CHECK(!std::as_const(patchy::ui::MainWindowTestAccess::document(window)).layers().front().mask().has_value());
}

void ui_selection_tool_context_menu_offers_subject_commands() {
  subject_selection_or_skip();
  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(disc_document(300, 240, QPoint(150, 120), 55), QStringLiteral("Context menu"));
  QApplication::processEvents();
  auto* canvas = require_canvas(window);
  require_action(window, "toolObjectSelectionAction")->trigger();
  QApplication::processEvents();
  const auto point = canvas->widget_position_for_document_point(QPoint(20, 20));
  send_mouse(*canvas, QEvent::MouseButtonPress, point, Qt::RightButton, Qt::RightButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, point, Qt::RightButton, Qt::NoButton);
  QApplication::processEvents();
  QMenu* menu = nullptr;
  for (auto* candidate : canvas->findChildren<QMenu*>(QStringLiteral("canvasContextMenu"))) {
    if (candidate->isVisible()) {
      menu = candidate;
    }
  }
  CHECK(menu != nullptr);
  QStringList names;
  for (auto* action : menu->actions()) {
    names << action->objectName();
    if (action->objectName() == QStringLiteral("selectSubjectAction") ||
        action->objectName() == QStringLiteral("selectBackgroundAction") ||
        action->objectName() == QStringLiteral("layerRemoveBackgroundAction")) {
      CHECK(action->icon().isNull());  // no letter-placeholder glyph beside the text
    }
  }
  CHECK(names.contains(QStringLiteral("selectSubjectAction")));
  CHECK(names.contains(QStringLiteral("selectBackgroundAction")));
  CHECK(names.contains(QStringLiteral("layerRemoveBackgroundAction")));
  menu->close();
  QApplication::processEvents();
}

void ui_select_object_in_marquee_selection_finds_the_object() {
  automatic_selection_or_skip();
  const QPoint center(150, 120);
  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(disc_document(300, 240, center, 55), QStringLiteral("Select Object"));
  QApplication::processEvents();
  auto* canvas = require_canvas(window);
  require_action_by_text(window, QStringLiteral("Marquee"))->trigger();
  QApplication::processEvents();
  const auto widget_point = [canvas](QPoint document_point) {
    return canvas->widget_position_for_document_point(document_point);
  };
  send_mouse(*canvas, QEvent::MouseButtonPress, widget_point(QPoint(70, 40)), Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseMove, widget_point(QPoint(150, 120)), Qt::NoButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseMove, widget_point(QPoint(232, 205)), Qt::NoButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, widget_point(QPoint(232, 205)), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(canvas->selected_document_region().contains(QPoint(75, 45)));  // the marquee's corner

  // The right-click menu inside the selection offers Select Object.
  send_mouse(*canvas, QEvent::MouseButtonPress, widget_point(center), Qt::RightButton, Qt::RightButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, widget_point(center), Qt::RightButton, Qt::NoButton);
  QApplication::processEvents();
  QAction* select_object = nullptr;
  for (auto* menu : canvas->findChildren<QMenu*>(QStringLiteral("canvasContextMenu"))) {
    if (menu->isVisible()) {
      for (auto* action : menu->actions()) {
        if (action->objectName() == QStringLiteral("selectObjectInSelectionAction")) {
          select_object = action;
        }
      }
      menu->close();
    }
  }
  QApplication::processEvents();
  CHECK(select_object != nullptr);
  if (select_object == nullptr) {
    return;
  }
  // Present AND shown: Qt keeps hidden actions in a menu's list (the subject-model hold-back
  // once hid this one while the list check still passed).
  CHECK(select_object->isVisible());
  CHECK(select_object->isEnabled());
  CHECK(select_object->icon().isNull());
  select_object->trigger();
  QApplication::processEvents();
  const auto region = canvas->selected_document_region();
  CHECK(region.contains(center));
  CHECK(!region.contains(QPoint(75, 45)));  // the object replaced the rectangle
  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  CHECK(canvas->selected_document_region().contains(QPoint(75, 45)));
}

// Select Object's first use offers the High Quality model once, like the tool's first
// activation: "Not Now" still selects the object (Standard model), and later uses never ask.
void ui_select_object_offers_the_high_quality_model_once() {
  automatic_selection_or_skip();
  patchy::ui::app_settings().remove(QStringLiteral("automaticSelection/highQualityModelOffered"));
  const QPoint center(150, 120);
  patchy::ui::MainWindow window;
  show_window(window);
  patchy::ui::MainWindowTestAccess::allow_high_quality_offer_offscreen(window);
  window.add_document_session(disc_document(300, 240, center, 55), QStringLiteral("Offer on Select Object"));
  QApplication::processEvents();
  auto* canvas = require_canvas(window);
  require_action_by_text(window, QStringLiteral("Marquee"))->trigger();
  QApplication::processEvents();
  const auto widget_point = [canvas](QPoint document_point) {
    return canvas->widget_position_for_document_point(document_point);
  };
  send_mouse(*canvas, QEvent::MouseButtonPress, widget_point(QPoint(70, 40)), Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseMove, widget_point(QPoint(232, 205)), Qt::NoButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, widget_point(QPoint(232, 205)), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(canvas->selected_document_region().contains(QPoint(75, 45)));

  // Answers "Not Now" to every offer that appears and counts them.
  int offers = 0;
  QTimer answer;
  answer.setInterval(20);
  QObject::connect(&answer, &QTimer::timeout, [&offers] {
    auto* box = qobject_cast<QMessageBox*>(find_top_level_dialog(QStringLiteral("objectSelectionModelOfferMessageBox")));
    if (box == nullptr || !box->isVisible()) {
      return;
    }
    for (auto* button : box->buttons()) {
      if (box->buttonRole(button) == QMessageBox::RejectRole) {
        ++offers;
        button->click();
        return;
      }
    }
  });
  answer.start();
  auto* select_object = require_action(window, "selectObjectInSelectionAction");
  select_object->trigger();
  QApplication::processEvents();
  CHECK(offers == 1);
  CHECK(patchy::ui::app_settings().value(QStringLiteral("automaticSelection/highQualityModelOffered")).toBool());
  auto region = canvas->selected_document_region();
  CHECK(region.contains(center));
  CHECK(!region.contains(QPoint(75, 45)));  // the object replaced the rectangle

  // A second use (and the tool's activation) never ask again.
  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  select_object->trigger();
  QApplication::processEvents();
  require_action(window, "toolObjectSelectionAction")->trigger();
  QApplication::processEvents();
  QApplication::processEvents();
  answer.stop();
  CHECK(offers == 1);
  CHECK(canvas->selected_document_region().contains(center));
}

// Runs a short script to completion and reports whether it ended without an error.
bool run_test_script(patchy::ui::MainWindow& window, const QString& source) {
  auto& host = window.script_engine_host();
  patchy::ui::ScriptEngineHost::RunOptions options;
  options.name = QStringLiteral("automatic-selection-sampling");
  (void)host.run_source(source, std::move(options));
  QElapsedTimer timer;
  timer.start();
  while (host.run_active() && timer.elapsed() < 120000) {
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 20);
  }
  QApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 20);
  if (host.last_run_had_error()) {
    for (const auto& line : host.message_backlog()) {
      std::printf("  %s\n", line.toUtf8().constData());
    }
  }
  return !host.run_active() && !host.last_run_had_error();
}

// Seth's case: a vector shape layer over a photo, Sample All Layers off, a loose box drag.
// The tool must analyze the shape layer alone, never a stale all-layers analysis.
void ui_object_select_samples_the_active_shape_layer_alone() {
  automatic_selection_or_skip();
  const QPoint center(150, 120);
  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(disc_document(300, 240, center, 55), QStringLiteral("Shape layer"));
  QApplication::processEvents();
  auto* canvas = require_canvas(window);
  require_action(window, "toolObjectSelectionAction")->trigger();
  QApplication::processEvents();
  canvas->set_selection_feather_radius(0);
  const auto widget_point = [canvas](QPoint document_point) {
    return canvas->widget_position_for_document_point(document_point);
  };
  // An all-layers analysis first, so a stale embedding exists.
  send_mouse(*canvas, QEvent::MouseButtonPress, widget_point(center), Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, widget_point(center), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(canvas->object_select_encodes_started() == 1);
  CHECK(canvas->selected_document_region().contains(center));

  CHECK(run_test_script(window, QStringLiteral(
      "app.activeDocument.addShape('Rectangle 1', {type: 'rectangle', x: 10, y: 10, width: 80, height: 60},"
      " {fill: '#ffcc33'});")));
  auto* sample_all = window.findChild<QCheckBox*>(QStringLiteral("objectSelectSampleAllLayersCheck"));
  CHECK(sample_all != nullptr);
  sample_all->setChecked(false);
  QApplication::processEvents();

  send_mouse(*canvas, QEvent::MouseButtonPress, widget_point(QPoint(2, 2)), Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseMove, widget_point(QPoint(70, 60)), Qt::NoButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseMove, widget_point(QPoint(135, 105)), Qt::NoButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, widget_point(QPoint(135, 105)), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(canvas->object_select_encodes_started() == 2);  // the shape layer was analyzed anew
  const auto region = canvas->selected_document_region();
  CHECK(region.contains(QPoint(50, 40)));      // inside the rectangle
  CHECK(!region.contains(QPoint(120, 95)));    // the photo inside the box stays out
  CHECK(!region.contains(center));             // and so does the earlier disc selection
  const auto bounds = region.boundingRect();
  CHECK(bounds.left() >= 6 && bounds.top() >= 6 && bounds.right() <= 94 && bounds.bottom() <= 74);
}

void ui_select_subject_follows_sample_all_layers() {
  subject_selection_or_skip();
  const QPoint center(150, 120);
  auto document = disc_document(300, 240, center, 55);
  patchy::PixelBuffer top(300, 240, patchy::PixelFormat::rgba8());
  top.clear(0);
  for (int y = 20; y < 90; ++y) {
    for (int x = 215; x < 285; ++x) {
      auto* px = top.pixel(x, y);
      px[0] = 40;
      px[1] = 170;
      px[2] = 60;
      px[3] = 255;
    }
  }
  document.add_pixel_layer("Square", std::move(top));
  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Sampling"));
  QApplication::processEvents();
  auto* canvas = require_canvas(window);
  canvas->set_object_select_sample_all_layers(false);
  require_action(window, "selectSubjectAction")->trigger();
  QApplication::processEvents();
  auto region = canvas->selected_document_region();
  CHECK(region.contains(QPoint(250, 55)));  // the active layer's square
  CHECK(!region.contains(center));          // the disc on the layer below is not sampled

  CHECK(run_test_script(window, QStringLiteral(R"JS(
    var d = app.activeDocument;
    d.selection.deselect();
    d.selection.selectSubject({sampleAllLayers: false});
    var b = d.selection.bounds;
    if (!b || b.x < 200 || b.y > 30 || b.x + b.width < 280) throw Error('layer-only subject ' + JSON.stringify(b));
  )JS")));

  canvas->set_object_select_sample_all_layers(true);
  require_action(window, "selectSubjectAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->selected_document_region().contains(center));
}

void ui_automatic_selection_refuses_an_empty_layer_without_sample_all() {
  automatic_selection_or_skip();
  auto document = disc_document(200, 160, QPoint(100, 80), 40);
  patchy::PixelBuffer empty(200, 160, patchy::PixelFormat::rgba8());
  empty.clear(0);
  document.add_pixel_layer("Empty", std::move(empty));
  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Empty layer"));
  QApplication::processEvents();
  auto* canvas = require_canvas(window);
  canvas->set_object_select_sample_all_layers(false);
  if (patchy::ai::subject_selection_available()) {
    require_action(window, "selectSubjectAction")->trigger();
    QApplication::processEvents();
    CHECK(!canvas->has_selection());
  }
  require_action(window, "toolObjectSelectionAction")->trigger();
  QApplication::processEvents();
  const auto point = canvas->widget_position_for_document_point(QPoint(100, 80));
  send_mouse(*canvas, QEvent::MouseButtonPress, point, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, point, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(!canvas->has_selection());
  CHECK(canvas->object_select_encodes_started() == 0);
}

void ui_script_select_background_and_remove_background() {
  subject_selection_or_skip();
  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(disc_document(300, 240, QPoint(150, 120), 55), QStringLiteral("Script background"));
  QApplication::processEvents();
  auto& host = window.script_engine_host();
  patchy::ui::ScriptEngineHost::RunOptions options;
  options.name = QStringLiteral("subject-background");
  (void)host.run_source(QStringLiteral(R"JS(
    var d = app.activeDocument;
    d.selection.selectBackground();
    var b = d.selection.bounds;
    if (!b || b.x !== 0 || b.y !== 0 || b.width !== 300 || b.height !== 240) throw Error('background ' + JSON.stringify(b));
    var threw = false;
    try { d.selection.selectBackground({inverse: true}); } catch (e) { threw = true; }
    if (!threw) throw Error('unknown key accepted');
    d.selection.deselect();
    d.activeLayer.removeBackground();
    if (d.selection.exists) throw Error('selection changed');
  )JS"), std::move(options));
  QElapsedTimer timer;
  timer.start();
  while (host.run_active() && timer.elapsed() < 120000) {
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 20);
  }
  QApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 20);
  CHECK(!host.run_active());
  if (host.last_run_had_error()) {
    for (const auto& line : host.message_backlog()) {
      std::printf("  %s\n", line.toUtf8().constData());
    }
  }
  CHECK(!host.last_run_had_error());
  const auto& layer = std::as_const(patchy::ui::MainWindowTestAccess::document(window)).layers().front();
  CHECK(layer.mask().has_value());
}

// While the subject model is held back, Object Selection still ships but the three subject
// commands are absent everywhere: menus, options bar, the selection-tool right-click menu
// and the script calls.
void ui_select_subject_is_left_out_without_the_subject_model() {
  automatic_selection_or_skip();
  if (patchy::ai::subject_selection_available()) {
    patchy::test::skip("build includes Select Subject");
  }
  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(disc_document(300, 240, QPoint(150, 120), 55), QStringLiteral("No subject"));
  QApplication::processEvents();
  for (const char* name : {"selectSubjectAction", "selectBackgroundAction", "layerRemoveBackgroundAction"}) {
    CHECK(!require_action(window, name)->isVisible());
  }
  CHECK(require_action(window, "toolObjectSelectionAction")->isVisible());
  // Select Object (right-click inside a selection) runs the Object Selection model and ships.
  CHECK(require_action(window, "selectObjectInSelectionAction")->isVisible());
  require_action(window, "toolObjectSelectionAction")->trigger();
  QApplication::processEvents();
  for (const auto* name : {"selectSubjectButton", "removeBackgroundButton"}) {
    auto* button = window.findChild<QPushButton*>(QString::fromLatin1(name));
    CHECK(button != nullptr && !button->isVisibleTo(&window));
  }
  auto* canvas = require_canvas(window);
  const auto point = canvas->widget_position_for_document_point(QPoint(20, 20));
  send_mouse(*canvas, QEvent::MouseButtonPress, point, Qt::RightButton, Qt::RightButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, point, Qt::RightButton, Qt::NoButton);
  QApplication::processEvents();
  for (auto* menu : canvas->findChildren<QMenu*>(QStringLiteral("canvasContextMenu"))) {
    if (menu->isVisible()) {
      for (auto* action : menu->actions()) {
        CHECK(action->objectName() != QStringLiteral("selectSubjectAction"));
      }
      menu->close();
    }
  }
  QApplication::processEvents();

  auto& host = window.script_engine_host();
  patchy::ui::ScriptEngineHost::RunOptions options;
  options.name = QStringLiteral("no-subject");
  (void)host.run_source(QStringLiteral(R"JS(
    var d = app.activeDocument;
    if (!d.selection.automaticSelectionAvailable) throw Error('object selection unavailable');
    if (d.selection.objectSelectionModel !== 'standard') throw Error('model ' + d.selection.objectSelectionModel);
    if (d.selection.subjectSelectionAvailable) throw Error('subject reported available');
    var calls = [function () { d.selection.selectSubject(); }, function () { d.selection.selectBackground(); },
                 function () { d.activeLayer.removeBackground(); }];
    calls.forEach(function (call, index) {
      var message = '';
      try { call(); } catch (e) { message = String(e); }
      if (message.indexOf('not included') < 0) throw Error('call ' + index + ': ' + message);
    });
    if (d.selection.exists) throw Error('selection changed');
    d.selection.objectSelect({point: {x: 150, y: 120}});
    if (!d.selection.exists) throw Error('objectSelect');
  )JS"), std::move(options));
  QElapsedTimer timer;
  timer.start();
  while (host.run_active() && timer.elapsed() < 120000) {
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 20);
  }
  QApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 20);
  CHECK(!host.run_active());
  if (host.last_run_had_error()) {
    for (const auto& line : host.message_backlog()) {
      std::printf("  %s\n", line.toUtf8().constData());
    }
  }
  CHECK(!host.last_run_had_error());
  CHECK(!std::as_const(patchy::ui::MainWindowTestAccess::document(window)).layers().front().mask().has_value());
}

}  // namespace

std::vector<patchy::test::TestCase> ai_selection_ui_tests() {
  return {
      {"ui_object_select_box_and_clicks_select_on_release", ui_object_select_box_and_clicks_select_on_release},
      {"ui_object_select_tool_and_options_are_listed", ui_object_select_tool_and_options_are_listed},
      {"ui_object_select_model_option_and_zoomed_box", ui_object_select_model_option_and_zoomed_box},
      {"ui_object_selection_model_download_verifies_files", ui_object_selection_model_download_verifies_files},
      {"ui_model_download_dialog_shows_the_link", ui_model_download_dialog_shows_the_link},
      {"ui_object_selection_model_offer_not_now_keeps_standard",
       ui_object_selection_model_offer_not_now_keeps_standard},
      {"ui_preferences_deletes_only_the_downloaded_model_files",
       ui_preferences_deletes_only_the_downloaded_model_files},
      {"ui_select_subject_menu_selects_the_subject", ui_select_subject_menu_selects_the_subject},
      {"ui_script_automatic_selection_selects_objects", ui_script_automatic_selection_selects_objects},
      {"ui_select_background_selects_everything_but_the_subject",
       ui_select_background_selects_everything_but_the_subject},
      {"ui_remove_background_masks_the_layer_in_one_undo_step", ui_remove_background_masks_the_layer_in_one_undo_step},
      {"ui_selection_tool_context_menu_offers_subject_commands",
       ui_selection_tool_context_menu_offers_subject_commands},
      {"ui_script_select_background_and_remove_background", ui_script_select_background_and_remove_background},
      {"ui_select_subject_is_left_out_without_the_subject_model",
       ui_select_subject_is_left_out_without_the_subject_model},
      {"ui_select_object_in_marquee_selection_finds_the_object",
       ui_select_object_in_marquee_selection_finds_the_object},
      {"ui_select_object_offers_the_high_quality_model_once", ui_select_object_offers_the_high_quality_model_once},
      {"ui_object_select_samples_the_active_shape_layer_alone",
       ui_object_select_samples_the_active_shape_layer_alone},
      {"ui_select_subject_follows_sample_all_layers", ui_select_subject_follows_sample_all_layers},
      {"ui_automatic_selection_refuses_an_empty_layer_without_sample_all",
       ui_automatic_selection_refuses_an_empty_layer_without_sample_all},
  };
}
