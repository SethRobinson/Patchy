// TrustMark watermarks in the app (docs/watermarks.md): the optional model download, its
// deletion from Preferences, and Image > Add Invisible Watermark. Scanning lives in File
// Info's Watermarks page (file_info_dialog.cpp). Nothing here removes a watermark.

#include "ui/main_window.hpp"

#include "ai/trustmark_watermark.hpp"
#include "ui/canvas_widget.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/invisible_watermark_dialog.hpp"
#include "ui/main_window_shared.hpp"
#include "ui/object_selection_model_download.hpp"
#include "ui/qt_geometry.hpp"
#include "ui/watermark_model_download.hpp"

#include <QEventLoop>
#include <QGuiApplication>
#include <QProgressDialog>
#include <QStatusBar>
#include <QTimer>

#include <exception>
#include <optional>
#include <utility>

namespace patchy::ui {
namespace {

int whole_megabytes(qint64 bytes) {
  return static_cast<int>((bytes + 500000) / 1000000);
}

}  // namespace

bool MainWindow::download_watermark_models(bool show_dialog, const std::function<void()>& keep_alive, QString* error,
                                           QWidget* dialog_parent) {
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
  return fail(tr("The watermark models are not available in the web version"));
#else
  configure_automatic_selection_models();
  if (patchy::ai::trustmark_models_available()) {
    return true;
  }
  if (!patchy::ai::trustmark_runtime_available()) {
    return fail(tr("this version of Patchy cannot run the watermark models"));
  }
  const auto total_mb = whole_megabytes(watermark_model_download_bytes());
  const auto progress_text = [total_mb](qint64 received) {
    return tr("Downloading the watermark models... %1 of %2 MB").arg(whole_megabytes(received)).arg(total_mb);
  };
  std::optional<QProgressDialog> dialog;
  if (show_dialog) {
    dialog.emplace(progress_text(0), tr("Cancel"), 0, 1000, dialog_parent != nullptr ? dialog_parent : this);
    dialog->setObjectName(QStringLiteral("watermarkModelDownloadDialog"));
    dialog->setWindowModality(Qt::WindowModal);
    dialog->setMinimumDuration(0);
    remember_dialog_position(*dialog);
    dialog->setValue(0);
  } else {
    statusBar()->showMessage(progress_text(0));
  }
  ModelDownloadResult result;
  bool done = false;
  QEventLoop wait_loop;
  auto job = start_model_download(
      this, downloaded_models_folder(), watermark_model_files(),
      [&](qint64 received, qint64 total) {
        if (dialog.has_value()) {
          dialog->setLabelText(progress_text(received));
          dialog->setValue(total > 0 ? static_cast<int>(received * 1000 / total) : 0);
        } else {
          statusBar()->showMessage(progress_text(received));
        }
      },
      [&](ModelDownloadResult finished) {
        result = std::move(finished);
        done = true;
        wait_loop.quit();
      });
  if (dialog.has_value()) {
    connect(&*dialog, &QProgressDialog::canceled, this, [&job] { job->cancel(); });
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
    if (!show_dialog) {
      statusBar()->clearMessage();
    }
    return fail(result.cancelled ? QString() : result.error);
  }
  if (!show_dialog) {
    statusBar()->showMessage(tr("Downloaded the watermark models"));
  }
  return patchy::ai::trustmark_models_available() ||
         fail(tr("the downloaded files are not where Patchy looks for them"));
#endif
}

bool MainWindow::delete_watermark_models(QString* error) {
#if defined(Q_OS_WASM)
  (void)error;
  return true;
#else
  configure_automatic_selection_models();
  // A run in flight keeps its own session reference; the files are read only at load.
  patchy::ai::release_trustmark_models();
  QString detail;
  const bool ok = delete_downloaded_watermark_models(downloaded_models_folder(), &detail);
  if (!ok && error != nullptr) {
    *error = detail;
  }
  return ok;
#endif
}

QString MainWindow::invisible_watermark_target_problem() {
  if (canvas_ == nullptr || !has_active_document()) {
    return tr("Open a document before adding an invisible watermark");
  }
  auto& doc = document();
  if (doc.color_state().bit_depth != BitDepth::UInt8) {
    return tr("Invisible watermarks need an 8-bit image. Convert it with Image > Mode > 8 Bits/Channel first.");
  }
  const auto active = doc.active_layer_id();
  const auto* layer = active.has_value() ? std::as_const(doc).find_layer(*active) : nullptr;
  if (layer == nullptr || layer->kind() != LayerKind::Pixel || std::as_const(*layer).pixels().empty()) {
    return tr("Select a pixel layer for the invisible watermark");
  }
  if (layer_id_locks_image_pixels(*active)) {
    return tr("Layer pixels are locked.");
  }
  // The mark is read back from the whole image, so it has to cover the whole image.
  const auto bounds = layer->bounds();
  if (bounds.x != 0 || bounds.y != 0 || bounds.width != doc.width() || bounds.height != doc.height()) {
    return tr("The active layer must fill the whole image for its watermark to be readable. Flatten the image or "
              "merge the layers first.");
  }
  return {};
}

bool MainWindow::apply_invisible_watermark(trustmark::Schema schema, const std::vector<std::uint8_t>& data_bits,
                                           float strength, QString* error) {
  const auto fail = [error](const QString& message) {
    if (error != nullptr) {
      *error = message;
    }
    return false;
  };
  finish_active_text_editor();
  if (const auto problem = invisible_watermark_target_problem(); !problem.isEmpty()) {
    return fail(problem);
  }
  configure_automatic_selection_models();
  if (!patchy::ai::trustmark_models_available()) {
    return fail(tr("The watermark models are not downloaded"));
  }
  auto& doc = document();
  auto* layer = doc.find_layer(*doc.active_layer_id());
  auto marked = std::as_const(*layer).pixels();
  const auto packet = trustmark::encode_packet(data_bits, schema);
  QGuiApplication::setOverrideCursor(Qt::WaitCursor);
  try {
    patchy::ai::embed_trustmark_q(marked, packet, strength);
  } catch (const std::exception& embed_error) {
    QGuiApplication::restoreOverrideCursor();
    return fail(tr("The watermark could not be added: %1").arg(QString::fromUtf8(embed_error.what())));
  }
  QGuiApplication::restoreOverrideCursor();
  push_undo_snapshot(tr("Add Invisible Watermark"));
  layer->set_pixels(std::move(marked));
  canvas_->document_changed(to_qrect(layer->bounds()));
  refresh_layer_list();
  return true;
}

void MainWindow::add_invisible_watermark() {
  finish_active_text_editor();
  if (!has_active_document()) {
    return;
  }
  if (const auto problem = invisible_watermark_target_problem(); !problem.isEmpty()) {
    show_status_error(problem);
    return;
  }
  configure_automatic_selection_models();
  InvisibleWatermarkDialogSupport support;
  support.models_available = [] { return patchy::ai::trustmark_models_available(); };
  if (patchy::ai::trustmark_runtime_available()) {
    support.download_models = [this](QWidget* parent) {
      QString error;
      const bool ok = download_watermark_models(true, {}, &error, parent);
      if (!ok && !error.isEmpty()) {
        show_status_error(tr("The watermark models could not be downloaded: %1").arg(error));
      }
      return ok;
    };
  }
  const auto request = request_invisible_watermark(this, support);
  if (!request.has_value()) {
    return;
  }
  QString error;
  if (!apply_invisible_watermark(request->schema, request->data_bits, request->strength, &error)) {
    show_status_error(error);
    return;
  }
  statusBar()->showMessage(request->text.isEmpty()
                               ? tr("Added an invisible watermark with the ID %1")
                                     .arg(QString::fromStdString(trustmark::bits_to_hex(request->data_bits)))
                               : tr("Added an invisible watermark carrying “%1”").arg(request->text));
}

}  // namespace patchy::ui
