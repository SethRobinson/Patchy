// TrustMark watermarks in the app (docs/watermarks.md): the optional model download, its
// deletion from Preferences, Image > Add Invisible Watermark, and Preferences > Watermarks'
// mark on save and check on open. Scanning lives in File Info's Watermarks page
// (file_info_dialog.cpp). Nothing here removes a watermark.

#include "ui/main_window.hpp"

#include "ai/trustmark_watermark.hpp"
#include "core/invisible_watermark.hpp"
#include "render/compositor.hpp"
#include "ui/activity_spinner.hpp"
#include "ui/background_workers.hpp"
#include "ui/canvas_widget.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/invisible_watermark_dialog.hpp"
#include "ui/main_window_shared.hpp"
#include "ui/object_selection_model_download.hpp"
#include "ui/qt_geometry.hpp"
#include "ui/watermark_model_download.hpp"

#include <QCheckBox>
#include <QCoreApplication>
#include <QEventLoop>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPointer>
#include <QProgressDialog>
#include <QPushButton>
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

// What one check on open found, posted back to the UI thread.
struct OpenWatermarkCheckResult {
  QStringList found;  // display names, untranslated product names
};

}  // namespace

std::optional<SaveWatermark> MainWindow::watermark_for_save(const QString& extension, const ImageSaveOptions& options,
                                                            bool quiet) {
  const auto mode = stored_watermark_save_mode();
  const auto text = stored_watermark_text();
  if (mode == WatermarkSaveMode::Never || text.isEmpty() || !has_active_document()) {
    return std::nullopt;
  }
  const bool animated = options.gif_animate || options.webp_animate || options.jxl_animate;
  if (!save_format_takes_watermark(std::as_const(document()), extension, options.tiff_layers.value_or(false),
                                   animated)) {
    return std::nullopt;
  }
  configure_automatic_selection_models();
  SaveWatermark watermark;
  watermark.text = text.toStdString();
  watermark.packet = trustmark::encode_packet(trustmark::text_to_bits(watermark.text), kSaveWatermarkSchema);
  watermark.strength = patchy::ai::kTrustMarkDefaultStrength;
  // Without the models the writer leaves the file unmarked and says so in the status bar;
  // there is nothing to ask about.
  if (mode == WatermarkSaveMode::Always || !patchy::ai::trustmark_models_available()) {
    return watermark;
  }
  if (quiet) {
    return std::nullopt;  // Ask never blocks a scripted or unattended save
  }
  QMessageBox box(this);
  box.setObjectName(QStringLiteral("saveWatermarkPrompt"));
  box.setIcon(QMessageBox::Question);
  box.setWindowTitle(tr("Add Invisible Watermark"));
  box.setText(tr("Add your invisible watermark \u201C%1\u201D to this file?").arg(text));
  box.setInformativeText(tr("Anyone with a TrustMark reader can read it. Only the saved file is marked; the open "
                            "document does not change."));
  auto* add_button = box.addButton(tr("Add Watermark"), QMessageBox::AcceptRole);
  auto* skip_button = box.addButton(tr("Save Without"), QMessageBox::RejectRole);
  box.setDefaultButton(add_button);
  box.setEscapeButton(skip_button);
  auto* remember = new QCheckBox(tr("Remember this choice"), &box);
  remember->setObjectName(QStringLiteral("saveWatermarkRememberCheck"));
  box.setCheckBox(remember);
  exec_dialog(box);
  const bool add = box.clickedButton() == add_button;
  if (remember->isChecked()) {
    set_stored_watermark_save_mode(add ? WatermarkSaveMode::Always : WatermarkSaveMode::Never);
  }
  if (!add) {
    return std::nullopt;
  }
  return watermark;
}

void MainWindow::start_open_watermark_check(std::int64_t session_id) {
  if (!stored_watermark_check_on_open() || unattended_automation()) {
    return;
  }
  auto* target = session_with_id(session_id);
  if (target == nullptr) {
    return;
  }
  configure_automatic_selection_models();
  // A copy-on-write snapshot: the worker only reads it, and an edit on the UI thread
  // detaches the live document's buffers instead of touching these.
  auto snapshot = std::make_shared<const Document>(std::as_const(*target).document_for_write());
  auto cancel = std::make_shared<patchy::ai::InferenceCancel>();
  cancel_open_watermark_check(session_id);
  watermark_checks_[session_id] = cancel;
  refresh_watermark_check_indicator();
  const bool trustmark = patchy::ai::trustmark_models_available();
  const auto file_name = QFileInfo(target->path).fileName();
  QPointer<MainWindow> self(this);
  run_tracked_background_worker([self, snapshot, cancel, trustmark, session_id, file_name] {
    OpenWatermarkCheckResult result;
    try {
      const auto flat = Compositor{}.flatten_rgb8(*snapshot);
      if (!cancel->requested()) {
        for (const auto& match : scan_invisible_watermarks(flat).matches) {
          result.found.push_back(QString::fromLatin1(match.name.data(), static_cast<qsizetype>(match.name.size())));
        }
      }
      if (trustmark && !cancel->requested()) {
        for (const auto& mark : patchy::ai::scan_trustmark(flat, cancel.get()).marks) {
          const auto variant = patchy::ai::trustmark_variant_name(mark.variant);
          result.found.push_back(
              QStringLiteral("Adobe TrustMark (%1): %2")
                  .arg(QString::fromLatin1(variant.data(), static_cast<qsizetype>(variant.size())),
                       mark.text.empty() ? QString::fromStdString(mark.hex)
                                         : QStringLiteral("\u201C%1\u201D").arg(QString::fromStdString(mark.text))));
        }
      }
    } catch (...) {
      // Cancelled, or the models could not run: a check that found nothing says nothing.
    }
    QMetaObject::invokeMethod(
        QCoreApplication::instance(),
        [self, cancel, session_id, file_name, result = std::move(result)] {
          if (self == nullptr) {
            return;
          }
          const auto running = self->watermark_checks_.find(session_id);
          if (running == self->watermark_checks_.end() || running->second != cancel) {
            return;  // cancelled, or replaced by a newer check
          }
          self->watermark_checks_.erase(running);
          self->refresh_watermark_check_indicator();
          if (!cancel->requested() && !result.found.isEmpty() && self->session_with_id(session_id) != nullptr) {
            self->show_watermark_found_notice(session_id, file_name, result.found);
          }
        },
        Qt::QueuedConnection);
  });
}

void MainWindow::cancel_open_watermark_check(std::int64_t session_id) {
  const auto running = watermark_checks_.find(session_id);
  if (running == watermark_checks_.end()) {
    return;
  }
  running->second->request();
  watermark_checks_.erase(running);
  refresh_watermark_check_indicator();
}

void MainWindow::refresh_watermark_check_indicator() {
  if (watermark_check_indicator_ == nullptr) {
    if (watermark_checks_.empty()) {
      return;
    }
    watermark_check_indicator_ = new QWidget(statusBar());
    watermark_check_indicator_->setObjectName(QStringLiteral("watermarkCheckIndicator"));
    auto* layout = new QHBoxLayout(watermark_check_indicator_);
    layout->setContentsMargins(4, 0, 4, 0);
    layout->setSpacing(5);
    auto* spinner = new ActivitySpinner(watermark_check_indicator_);
    spinner->setObjectName(QStringLiteral("watermarkCheckSpinner"));
    auto* label = new QLabel(watermark_check_indicator_);
    label->setObjectName(QStringLiteral("watermarkCheckLabel"));
    layout->addWidget(spinner);
    layout->addWidget(label);
    statusBar()->addPermanentWidget(watermark_check_indicator_);
  }
  const bool running = !watermark_checks_.empty();
  if (auto* label = watermark_check_indicator_->findChild<QLabel*>(QStringLiteral("watermarkCheckLabel"))) {
    label->setText(tr("Checking for watermarks..."));
  }
  // ActivitySpinner has no Q_OBJECT, so it is found by name.
  if (auto* spinner = watermark_check_indicator_->findChild<QWidget*>(QStringLiteral("watermarkCheckSpinner"))) {
    static_cast<ActivitySpinner*>(spinner)->set_active(running);
  }
  watermark_check_indicator_->setVisible(running);
}

void MainWindow::show_watermark_found_notice(std::int64_t session_id, const QString& file_name,
                                             const QStringList& found) {
  // Non-modal and self-deleting: the check finishes whenever it finishes, and the notice
  // must never block work in the window.
  auto* box = new QMessageBox(this);
  box->setObjectName(QStringLiteral("watermarkFoundNotice"));
  box->setAttribute(Qt::WA_DeleteOnClose);
  box->setModal(false);
  box->setWindowModality(Qt::NonModal);
  box->setIcon(QMessageBox::Information);
  box->setWindowTitle(tr("Watermark Found"));
  box->setText(file_name.isEmpty() ? tr("This image carries an invisible watermark.")
                                   : tr("\u201C%1\u201D carries an invisible watermark.").arg(file_name));
  box->setInformativeText(found.join(QLatin1Char('\n')));
  auto* show_button = box->addButton(tr("Show in File Info"), QMessageBox::AcceptRole);
  show_button->setObjectName(QStringLiteral("watermarkFoundShowButton"));
  box->addButton(QMessageBox::Close);
  QPointer<MainWindow> self(this);
  connect(box, &QMessageBox::buttonClicked, this, [self, show_button, session_id](QAbstractButton* clicked) {
    if (self == nullptr || clicked != show_button) {
      return;
    }
    // After the notice has closed, so File Info's own dialog is not stacked under it.
    QTimer::singleShot(0, self, [self, session_id] {
      if (self == nullptr) {
        return;
      }
      if (auto* target = self->session_with_id(session_id)) {
        self->activate_document_session(*target);
        self->show_file_info_dialog(/*watermarks_scan*/ true);
      }
    });
  });
  box->show();
}

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
