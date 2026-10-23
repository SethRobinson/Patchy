// Select > Select Subject, Select > Select Background and Layer > Remove Background
// (docs/ai-selection.md). Each runs one BiRefNet pass over the merged document on a worker
// thread, under a cancellable progress dialog. The soft matte then combines with the
// selection, is inverted, or becomes a layer mask: plain booleans on the one result. One
// pass over the whole image only: no network-located crop and no second pass (patent
// boundary in src/ai/segmentation_engine.hpp).

#include "core/pixel_buffer.hpp"
#include "core/layer.hpp"
#include "ui/main_window.hpp"
#include "ui/model_download_dialog.hpp"
#include <QComboBox>
#include <QPushButton>
#include <QStatusBar>

#include "ai/segmentation_engine.hpp"
#include "core/layer_render_utils.hpp"
#include "core/pixel_depth.hpp"
#include "core/rect_utils.hpp"
#include "ui/background_workers.hpp"
#include "ui/canvas_widget.hpp"
#include "ui/canvas_widget_shared.hpp"
#include "ui/app_settings.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/image_document_io.hpp"
#include "ui/main_window_shared.hpp"
#include "ui/object_selection_model_download.hpp"
#include "ui/qt_geometry.hpp"
#include "ui/qt_paths.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QLatin1String>
#include <QMessageBox>
#include <QPointer>
#include <QProgressDialog>
#include <QRect>
#include <QSignalBlocker>
#include <QString>
#include <QStringLiteral>
#include <QTimer>
#include <QWidget>
#include <Qt>
#include <QtGlobal>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <exception>
#include <functional>
#include <future>
#include <optional>
#include <utility>

namespace patchy::ui {
namespace {

// How long the subject model stays loaded after the last subject command.
constexpr int kSubjectModelIdleReleaseMs = 3 * 60 * 1000;

QImage grayscale_image(const PixelBuffer& gray8) {
  QImage image(gray8.width(), gray8.height(), QImage::Format_Grayscale8);
  for (int y = 0; y < gray8.height(); ++y) {
    std::memcpy(image.scanLine(y), gray8.row(y).data(), static_cast<std::size_t>(gray8.width()));
  }
  return image;
}

// A painted mask plane's value at document (x, y) as 0..255, whatever its depth;
// default_color outside its bounds.
std::uint8_t painted_mask_value(const LayerMask& mask, std::int32_t x, std::int32_t y) {
  if (!mask.bounds.contains(x, y) || mask.pixels.empty()) {
    return mask.default_color;
  }
  const auto coverage = coverage_at(mask.pixels, x - mask.bounds.x, y - mask.bounds.y);
  return static_cast<std::uint8_t>(std::clamp(std::lround(coverage * 255.0F), 0L, 255L));
}

}  // namespace

void apply_subject_matte_to_layer_mask(const Document& document, Layer& layer, const patchy::ai::AiMask& matte) {
  // The new mask covers the subject; with an existing mask the two multiply, so anything
  // already hidden stays hidden and the mask keeps its density and feather settings.
  PixelBuffer coverage = matte.alpha;
  const std::optional<LayerMask> existing = std::as_const(layer).mask();
  if (existing.has_value()) {
    for (std::int32_t y = 0; y < matte.bounds.height; ++y) {
      auto row = coverage.row(y);
      for (std::int32_t x = 0; x < matte.bounds.width; ++x) {
        const auto old_value = painted_mask_value(*existing, matte.bounds.x + x, matte.bounds.y + y);
        auto& value = row[static_cast<std::size_t>(x)];
        value = static_cast<std::uint8_t>((static_cast<unsigned>(value) * old_value + 127U) / 255U);
      }
    }
  }
  LayerMask mask = existing.value_or(LayerMask{});
  mask.bounds = matte.bounds;
  mask.pixels = coverage_at_document_depth(document, std::move(coverage));
  mask.default_color = 0;
  layer.set_mask(std::move(mask));
}

void configure_automatic_selection_models() {
  configure_downloaded_object_selection_model();
  if (!patchy::ai::model_directory().empty()) {
    return;
  }
  const QDir executable_directory(QCoreApplication::applicationDirPath());
  for (const auto* relative : {"ai-models", "../Resources/ai-models", "../share/patchy/ai-models"}) {
    const auto candidate = executable_directory.absoluteFilePath(QLatin1String(relative));
    if (QFileInfo(candidate).isDir()) {
      patchy::ai::set_model_directory(to_filesystem_path(QDir::cleanPath(candidate)));
      return;
    }
  }
}

namespace {

#if !defined(Q_OS_WASM)
// Persisted settings key (never rename): the first-use offer was shown. The web build
// never offers the download.
constexpr auto kHighQualityOfferedKey = "automaticSelection/highQualityModelOffered";
#endif

// Decimal megabytes, as download sizes are usually given.
int megabytes(qint64 bytes) {
  return static_cast<int>((bytes + 500000) / 1000000);
}

}  // namespace

void MainWindow::maybe_offer_high_quality_object_selection_model() {
#if !defined(Q_OS_WASM)
  configure_automatic_selection_models();
  // Offscreen runs are tests (they opt in through MainWindowTestAccess) or headless work.
  const bool offscreen = QGuiApplication::platformName() == QLatin1String("offscreen");
  if (!patchy::ai::segmentation_available() || patchy::ai::high_quality_model_available() ||
      unattended_automation() || (offscreen && !high_quality_offer_in_offscreen_tests_)) {
    return;
  }
  auto settings = app_settings();
  if (settings.value(QLatin1String(kHighQualityOfferedKey), false).toBool()) {
    return;
  }
  settings.setValue(QLatin1String(kHighQualityOfferedKey), true);
  offer_high_quality_object_selection_model();
#endif
}

bool MainWindow::offer_high_quality_object_selection_model() {
  QMessageBox box(this);
  box.setObjectName(QStringLiteral("objectSelectionModelOfferMessageBox"));
  box.setIcon(QMessageBox::Question);
  box.setWindowTitle(tr("High Quality Object Selection"));
  box.setText(tr("Download the High Quality model for Object Selection?"));
  box.setInformativeText(tr("It finds objects much more accurately than the built-in model. The download is %1 MB "
                            "from %2 and is kept on this computer. You can switch models at any time on the "
                            "Object Selection options bar.")
                             .arg(megabytes(high_quality_model_download_bytes()))
                             .arg(model_download_source_text(high_quality_model_files())));
  auto* download_button = box.addButton(tr("Download"), QMessageBox::AcceptRole);
  box.addButton(tr("Not Now"), QMessageBox::RejectRole);
  box.setDefaultButton(download_button);
  exec_dialog(box);
  if (box.clickedButton() != download_button) {
    refresh_object_select_model_combo();
    return false;
  }
  QString error;
  const bool ok = download_high_quality_object_selection_model(true, {}, &error);
  if (ok) {
    set_object_selection_high_quality_setting(true);
    statusBar()->showMessage(tr("Object Selection now uses the High Quality model"));
  } else if (!error.isEmpty()) {
    show_status_error(tr("The High Quality model could not be downloaded: %1").arg(error));
  } else {
    statusBar()->showMessage(tr("Download cancelled"));
  }
  refresh_object_select_model_combo();
  return ok;
}

bool MainWindow::download_high_quality_object_selection_model(bool show_dialog, const std::function<void()>& keep_alive,
                                                              QString* error, QWidget* dialog_parent) {
  const auto fail = [error](const QString& message) {
    if (error != nullptr) {
      *error = message;
    }
    return false;
  };
#if defined(Q_OS_WASM)
  (void)show_dialog;
  (void)keep_alive;
  (void)dialog_parent;
  return fail(tr("The High Quality model is not available in the web version"));
#else
  configure_automatic_selection_models();
  if (patchy::ai::high_quality_model_available()) {
    return true;
  }
  const auto total_mb = megabytes(high_quality_model_download_bytes());
  const auto progress_text = [total_mb](qint64 received) {
    return tr("Downloading the High Quality model... %1 of %2 MB").arg(megabytes(received)).arg(total_mb);
  };
  std::optional<ModelDownloadDialog> dialog;
  if (show_dialog) {
    dialog.emplace(dialog_parent != nullptr ? dialog_parent : this);
    dialog->setObjectName(QStringLiteral("objectSelectionModelDownloadDialog"));
    dialog->setWindowTitle(tr("High Quality Object Selection"));
    dialog->set_heading(progress_text(0));
    remember_dialog_position(*dialog);
    dialog->show();
  } else {
    statusBar()->showMessage(progress_text(0));
  }
  ModelDownloadResult result;
  bool done = false;
  QEventLoop wait_loop;
  auto job = start_model_download(
      this, downloaded_models_folder(), high_quality_model_files(),
      [&](const ModelDownloadProgress& progress) {
        if (dialog.has_value()) {
          dialog->set_heading(progress_text(progress.received));
          dialog->set_progress(progress);
        } else {
          statusBar()->showMessage(progress_text(progress.received));
        }
      },
      [&](ModelDownloadResult finished) {
        result = std::move(finished);
        done = true;
        wait_loop.quit();
      });
  if (dialog.has_value()) {
    connect(&*dialog, &QDialog::rejected, this, [&job] { job->cancel(); });
  }
  QTimer keep_alive_timer;
  if (keep_alive) {
    keep_alive_timer.setInterval(200);
    connect(&keep_alive_timer, &QTimer::timeout, this, [&keep_alive] { keep_alive(); });
    keep_alive_timer.start();
  }
  if (!done) {
    wait_loop.exec(show_dialog ? QEventLoop::AllEvents : QEventLoop::ExcludeUserInputEvents);
  }
  keep_alive_timer.stop();
  dialog.reset();
  if (!result.ok) {
    return fail(result.cancelled ? QString() : result.error);
  }
  // A cached Standard embedding no longer matches the model that will run (the canvas key
  // includes the model, so the next press re-encodes).
  return patchy::ai::high_quality_model_available() ||
         fail(tr("the downloaded files are not where Patchy looks for them"));
#endif
}

bool MainWindow::delete_high_quality_object_selection_model(QString* error) {
#if defined(Q_OS_WASM)
  (void)error;
  return true;
#else
  configure_automatic_selection_models();
  // A run in flight keeps its own session reference; the files are read only at load.
  patchy::ai::release_high_quality_model();
  QString detail;
  const bool ok = delete_downloaded_high_quality_model(downloaded_models_folder(), &detail);
  if (ok) {
    // Back to a fresh start: the tool offers the download again on its next first use, and
    // a new download is used without picking High Quality again.
    app_settings().remove(QLatin1String(kHighQualityOfferedKey));
    set_object_selection_high_quality_setting(true);
  }
  refresh_object_select_model_combo();
  if (!ok && error != nullptr) {
    *error = detail;
  }
  return ok;
#endif
}

void MainWindow::refresh_object_select_model_combo() {
  if (object_select_model_combo_ == nullptr) {
    return;
  }
  const QSignalBlocker blocker(object_select_model_combo_);
  object_select_model_combo_->setCurrentIndex(patchy::ai::active_sam_model() == patchy::ai::SamModel::HighQuality ? 1
                                                                                                                  : 0);
}

std::optional<patchy::ai::AiMask> MainWindow::compute_subject_matte(const QString& command, QString* error) {
  const auto fail = [error](const QString& message) -> std::optional<patchy::ai::AiMask> {
    if (error != nullptr) {
      *error = message;
    }
    return std::nullopt;
  };
  if (canvas_ == nullptr) {
    return fail(tr("Open a document before using %1").arg(command));
  }
  if (!patchy::ai::subject_selection_available()) {
    return fail(tr("%1 is not available in this build").arg(command));
  }
  if (canvas_->quick_mask_active()) {
    return fail(tr("%1 is unavailable in Quick Mask mode").arg(command));
  }
  // Quick Select's bar has its own Sample All Layers; everywhere else (menu, right-click,
  // the Object Selection bar) the Object Selection tool's checkbox decides.
  const bool sample_all_layers = current_tool_ == CanvasTool::QuickSelect
                                     ? canvas_->quick_select_sample_all_layers()
                                     : canvas_->object_select_sample_all_layers();
  QString source_error;
  const auto composite = canvas_->automatic_selection_source(sample_all_layers, &source_error);
  if (composite.isNull()) {
    return fail(source_error.isEmpty() ? tr("%1 could not read the document").arg(command) : source_error);
  }
  auto pixels = pixels_from_image_rgba(composite);

  if (subject_model_release_timer_ == nullptr) {
    subject_model_release_timer_ = new QTimer(this);
    subject_model_release_timer_->setSingleShot(true);
    subject_model_release_timer_->setInterval(kSubjectModelIdleReleaseMs);
    connect(subject_model_release_timer_, &QTimer::timeout, this, [] { patchy::ai::release_subject_model(); });
  }
  // The model must not be freed under the running worker.
  subject_model_release_timer_->stop();

  const QPointer<CanvasWidget> canvas = canvas_;
  patchy::ai::InferenceCancel cancel;
  auto future = launch_async([pixels = std::move(pixels), &cancel] { return patchy::ai::subject_matte(pixels, &cancel); });
  // The model reports no progress of its own: the percent is an estimate from how long the
  // last run took on this machine (persisted), held below 100 until the run really ends.
  auto settings = app_settings();
  const auto estimate_ms = std::clamp(
      settings.value(QStringLiteral("automaticSelection/subjectRunMs"), 7000).toLongLong(), 500LL, 120000LL);
  QElapsedTimer run_timer;
  run_timer.start();
  {
    QProgressDialog progress(tr("Finding the subject..."), tr("Cancel"), 0, 100, this);
    progress.setObjectName(QStringLiteral("selectSubjectProgressDialog"));
    progress.setWindowModality(Qt::WindowModal);
    // Shown at once (the run always takes seconds): the window-modal dialog is what keeps
    // the document from changing under the worker.
    progress.setMinimumDuration(0);
    remember_dialog_position(progress);
    connect(&progress, &QProgressDialog::canceled, this, [&cancel] { cancel.request(); });
    canvas->begin_processing_operation();
    QEventLoop wait_loop;
    QTimer poll;
    poll.setInterval(30);
    connect(&poll, &QTimer::timeout, &wait_loop, [&] {
      const auto percent = static_cast<int>(std::clamp<qint64>(run_timer.elapsed() * 100 / estimate_ms, 1, 95));
      progress.setValue(percent);
      if (canvas != nullptr) {
        canvas->set_processing_operation_message(tr("Finding the subject... %1%").arg(percent));
        canvas->tick_processing_operation();
      }
      if (future.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
        wait_loop.quit();
      }
    });
    if (future.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
      poll.start();
      wait_loop.exec();
    }
    if (canvas != nullptr) {
      canvas->end_processing_operation();
    }
  }
  subject_model_release_timer_->start();

  patchy::ai::AiMask matte;
  try {
    matte = future.get();
    // Blend the measured run into the stored estimate (load time included on purpose).
    settings.setValue(QStringLiteral("automaticSelection/subjectRunMs"),
                      static_cast<qlonglong>((estimate_ms + run_timer.elapsed()) / 2));
  } catch (const patchy::ai::InferenceCancelled&) {
    return fail(tr("%1 cancelled").arg(command));
  } catch (const std::exception& failure) {
    if (patchy::ai::is_out_of_memory(failure)) {
      return fail(automatic_selection_out_of_memory_message(command));
    }
    return fail(tr("%1 failed: %2").arg(command, QString::fromUtf8(failure.what())));
  }
  if (canvas == nullptr || canvas != canvas_) {
    return fail(tr("%1 cancelled").arg(command));
  }
  if (matte.empty()) {
    return fail(tr("%1 found no subject").arg(command));
  }
  return matte;
}

bool MainWindow::run_select_subject(CanvasWidget::SelectionMode mode, QString* error) {
  const auto matte = compute_subject_matte(tr("Select Subject"), error);
  if (!matte.has_value()) {
    return false;
  }
  canvas_->apply_automatic_selection_mask(
      QRect(matte->bounds.x, matte->bounds.y, matte->bounds.width, matte->bounds.height), grayscale_image(matte->alpha),
      mode, tr("Select Subject"));
  return true;
}

bool MainWindow::run_select_background(CanvasWidget::SelectionMode mode, QString* error) {
  const auto matte = compute_subject_matte(tr("Select Background"), error);
  if (!matte.has_value()) {
    return false;
  }
  // Everything the subject matte does not cover: 255 outside its bounds, inverted inside.
  const auto& doc = document();
  PixelBuffer background(doc.width(), doc.height(), PixelFormat::gray8());
  background.clear(255);
  for (std::int32_t y = 0; y < matte->bounds.height; ++y) {
    const auto source = matte->alpha.row(y);
    auto target = background.row(matte->bounds.y + y);
    for (std::int32_t x = 0; x < matte->bounds.width; ++x) {
      target[static_cast<std::size_t>(matte->bounds.x + x)] =
          static_cast<std::uint8_t>(255U - source[static_cast<std::size_t>(x)]);
    }
  }
  canvas_->apply_automatic_selection_mask(QRect(0, 0, doc.width(), doc.height()), grayscale_image(background), mode,
                                          tr("Select Background"));
  return true;
}

bool MainWindow::run_remove_background(QString* error) {
  const auto fail = [error](const QString& message) {
    if (error != nullptr) {
      *error = message;
    }
    return false;
  };
  if (canvas_ == nullptr) {
    return fail(tr("Open a document before using %1").arg(tr("Remove Background")));
  }
  auto& doc = document();
  select_only_layer_if_none_active();
  const auto active = doc.active_layer_id();
  const auto* candidate = active.has_value() ? std::as_const(doc).find_layer(*active) : nullptr;
  if (candidate == nullptr || (candidate->kind() != LayerKind::Pixel && candidate->kind() != LayerKind::Adjustment &&
                               candidate->kind() != LayerKind::Group)) {
    return fail(tr("Select a pixel, adjustment, or group layer before adding a mask"));
  }
  if (layer_id_locks_image_pixels(*active)) {
    return fail(tr("Layer pixels are locked."));
  }
  const auto matte = compute_subject_matte(tr("Remove Background"), error);
  if (!matte.has_value()) {
    return false;
  }
  auto* layer = doc.find_layer(*active);
  if (layer == nullptr) {
    return fail(tr("Select a pixel, adjustment, or group layer before adding a mask"));
  }

  push_undo_snapshot(tr("Remove Background"));
  const auto before = layer_render_bounds(*layer);
  apply_subject_matte_to_layer_mask(doc, *layer, *matte);
  const auto after = layer_render_bounds(*layer);
  canvas_->invalidate_mask_display();
  canvas_->document_changed(to_qrect(unite_rect(before, after)));
  refresh_layer_list();
  return true;
}

void MainWindow::select_subject(CanvasWidget::SelectionMode mode) {
  QString error;
  if (run_select_subject(mode, &error)) {
    statusBar()->showMessage(tr("Selected the subject"));
  } else if (!error.isEmpty()) {
    show_status_error(error);
  }
}

void MainWindow::select_background(CanvasWidget::SelectionMode mode) {
  QString error;
  if (run_select_background(mode, &error)) {
    statusBar()->showMessage(tr("Selected the background"));
  } else if (!error.isEmpty()) {
    show_status_error(error);
  }
}

void MainWindow::remove_background() {
  QString error;
  if (run_remove_background(&error)) {
    statusBar()->showMessage(tr("Removed the background with a layer mask"));
  } else if (!error.isEmpty()) {
    show_status_error(error);
  }
}

}  // namespace patchy::ui
