// Automatic document recovery (docs/document-recovery.md): the timer, the
// background write of one PSB copy per modified document, the lifecycle hooks'
// shared discard, and the reopen of copies a crashed instance left behind.

#include "ui/main_window.hpp"
#include "ui/main_window_shared.hpp"

#include "core/document_recovery_store.hpp"
#include "psd/psd_document_io.hpp"
#include "support/path_utils.hpp"
#include "ui/app_settings.hpp"
#include "ui/background_workers.hpp"
#include "ui/canvas_widget.hpp"
#include "ui/document_recovery.hpp"
#include "ui/qt_paths.hpp"

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QPointer>
#include <QStatusBar>
#include <QTextEdit>
#include <QTimer>

#include <exception>
#include <memory>
#include <system_error>
#include <utility>

namespace patchy::ui {

bool MainWindow::any_canvas_interaction_active() const {
  for (const auto& target_session : sessions_) {
    const auto* canvas = target_session != nullptr ? target_session->canvas : nullptr;
    if (canvas != nullptr &&
        (canvas->pointer_gesture_active() || canvas->free_transform_active() || canvas->warp_transform_active() ||
         canvas->path_transform_active() || canvas->crop_session_active() ||
         canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor")) != nullptr)) {
      return true;
    }
  }
  return false;
}

#ifndef Q_OS_WASM

namespace {

struct RecoveryJob {
  MainWindow::RecoveryMark mark;
  Document snapshot;
  recovery::RecoveryEntry entry;
};

QString original_path_from_entry(const recovery::RecoveryEntry& entry) {
  if (entry.original_path.empty()) {
    return QString();
  }
  return to_qstring(std::filesystem::path(std::u8string(entry.original_path.begin(), entry.original_path.end())));
}

}  // namespace

void MainWindow::start_document_recovery() {
  recovery_folder_ = std::make_shared<RecoveryInstanceFolder>(RecoveryInstanceFolder::recovery_root());
  recovery_timer_ = new QTimer(this);
  recovery_timer_->setObjectName(QStringLiteral("documentRecoveryTimer"));
  recovery_timer_->setTimerType(Qt::VeryCoarseTimer);
  connect(recovery_timer_, &QTimer::timeout, this, [this] {
    // Export, run-script, headless and hidden-connector instances never write: their
    // documents are the automation's, not the user's, and nobody would recover them.
    if (!cli_automation_mode_) {
      write_recovery_now(/*wait=*/false);
    }
  });
  apply_recovery_preferences();
}

void MainWindow::apply_recovery_preferences() {
  if (recovery_timer_ == nullptr) {
    return;
  }
  if (!stored_recovery_enabled()) {
    recovery_timer_->stop();
    return;
  }
  int interval_ms = stored_recovery_interval_minutes() * 60 * 1000;
  // Test hook: a short interval makes the timer observable inside a test's budget.
  if (const auto override_ms = qEnvironmentVariable("PATCHY_RECOVERY_INTERVAL_MS").toInt(); override_ms > 0) {
    interval_ms = override_ms;
  }
  recovery_timer_->start(interval_ms);
}

bool MainWindow::recovery_busy() const {
  return QApplication::activeModalWidget() != nullptr || preview_dialog_edit_locked() ||
         any_canvas_interaction_active();
}

QString MainWindow::recovery_directory() const {
  return recovery_folder_ != nullptr ? recovery_folder_->directory_string() : QString();
}

std::vector<recovery::RecoveryEntry> MainWindow::list_recovery_entries() const {
  if (recovery_folder_ == nullptr || !recovery_folder_->created()) {
    return {};
  }
  return recovery::scan_instance_dir(recovery_folder_->directory());
}

std::vector<OrphanedRecoveryFolder> MainWindow::list_orphaned_recovery() const {
  return RecoveryInstanceFolder::scan_orphaned(RecoveryInstanceFolder::recovery_root());
}

void MainWindow::discard_recovery_for_session(std::int64_t session_id) {
  recovery_marks_.erase(session_id);
  if (recovery_folder_ != nullptr && recovery_folder_->created()) {
    recovery::remove_entry(recovery_folder_->directory(), session_id);
  }
}

QStringList MainWindow::write_recovery_now(bool wait) {
  QStringList written;
  if (recovery_folder_ == nullptr || recovery_write_in_flight_ || recovery_busy()) {
    return written;
  }
  auto jobs = std::make_shared<std::vector<RecoveryJob>>();
  for (const auto& target_session : sessions_) {
    if (target_session == nullptr || !session_is_modified(*target_session)) {
      continue;
    }
    const auto mark = recovery_marks_.find(target_session->session_id);
    if (mark != recovery_marks_.end() && mark->second.revision == target_session->revision &&
        mark->second.state_id == target_session->current_state_id) {
      continue;
    }
    RecoveryJob job;
    job.mark = RecoveryMark{target_session->session_id, target_session->revision, target_session->current_state_id};
    // A copy shares pixel storage with the live document until the user's next edit
    // detaches the live side; the worker only reads it (const Document&).
    job.snapshot = std::as_const(*target_session).document;
    job.entry.title = target_session->title.toStdString();
    job.entry.original_path =
        target_session->path.isEmpty() ? std::string() : path_to_utf8(to_filesystem_path(target_session->path));
    jobs->push_back(std::move(job));
  }
  if (jobs->empty()) {
    return written;
  }
  if (!recovery_folder_->ensure_created()) {
    show_status_error(tr("Could not save recovery information: %1")
                          .arg(QDir::toNativeSeparators(recovery_folder_->directory_string())));
    return written;
  }
  const auto directory = recovery_folder_->directory();
  for (const auto& job : *jobs) {
    written.push_back(to_qstring(recovery::document_path(directory, job.mark.session_id)));
  }
  recovery_write_in_flight_ = true;
  auto folder = recovery_folder_;
  auto* app = QApplication::instance();
  QPointer<MainWindow> self(this);
  run_tracked_background_worker([app, self, folder, jobs] {
    auto marks = std::make_shared<std::vector<RecoveryMark>>();
    auto errors = std::make_shared<QStringList>();
    for (auto& job : *jobs) {
      try {
        job.entry.saved_at_unix_ms = QDateTime::currentMSecsSinceEpoch();
        const auto bytes = psd::DocumentIo::write_layered_rgb8(job.snapshot, psd::WriteOptions{true});
        recovery::write_entry(folder->directory(), job.mark.session_id, bytes, job.entry);
        marks->push_back(job.mark);
      } catch (const std::exception& error) {
        errors->push_back(QString::fromUtf8(error.what()));
      }
    }
    // Release the snapshots here, off the UI thread.
    jobs->clear();
    if (app == nullptr) {
      return;
    }
    QMetaObject::invokeMethod(
        app,
        [self, folder, marks, errors] {
          if (self != nullptr) {
            self->finish_recovery_write(*marks, *errors);
          }
        },
        Qt::QueuedConnection);
  });
  if (wait) {
    while (recovery_write_in_flight_) {
      QApplication::processEvents(QEventLoop::AllEvents, 15);
    }
  }
  return written;
}

void MainWindow::finish_recovery_write(const std::vector<RecoveryMark>& marks, const QStringList& errors) {
  recovery_write_in_flight_ = false;
  if (recovery_folder_ == nullptr) {
    return;
  }
  for (const auto& mark : marks) {
    const auto* target_session = session_with_id(mark.session_id);
    if (target_session == nullptr || !session_is_modified(*target_session)) {
      // Closed or saved while the copy was being written: the copy is stale, and a
      // close or save already removed the previous one, so remove this one too.
      recovery::remove_entry(recovery_folder_->directory(), mark.session_id);
      recovery_marks_.erase(mark.session_id);
      continue;
    }
    recovery_marks_[mark.session_id] = mark;
  }
  if (!errors.isEmpty()) {
    show_status_error(tr("Could not save recovery information: %1").arg(errors.front()));
  }
}

std::vector<std::int64_t> MainWindow::recover_orphaned_documents() {
  std::vector<std::int64_t> recovered;
  if (recovery_folder_ == nullptr) {
    return recovered;
  }
  int failed = 0;
  for (const auto& orphan : list_orphaned_recovery()) {
    bool folder_clean = true;
    for (const auto& entry : orphan.entries) {
      const auto psb_path = orphan.directory / std::filesystem::path(entry.file_stem + std::string(recovery::kDocumentExtension));
      const auto sidecar_path =
          orphan.directory / std::filesystem::path(entry.file_stem + std::string(recovery::kSidecarExtension));
      const auto name = entry.title.empty() ? tr("Untitled") : QString::fromStdString(entry.title);
      std::int64_t session_id = 0;
      if (!open_recovered_document(to_qstring(psb_path), tr("%1 (Recovered)").arg(name),
                                   original_path_from_entry(entry), &session_id)) {
        ++failed;
        folder_clean = false;
        continue;
      }
      recovered.push_back(session_id);
      // Keep the copy under this instance so a second crash is covered too; the
      // session's current state IS the copy, so the timer need not rewrite it.
      if (auto* target_session = session_with_id(session_id);
          target_session != nullptr && recovery_folder_->ensure_created()) {
        std::error_code error;
        std::filesystem::rename(psb_path, recovery::document_path(recovery_folder_->directory(), session_id), error);
        if (!error) {
          std::filesystem::rename(sidecar_path, recovery::sidecar_path(recovery_folder_->directory(), session_id),
                                  error);
          recovery_marks_[session_id] =
              RecoveryMark{session_id, target_session->revision, target_session->current_state_id};
        }
      }
      recovery::remove_entry(orphan.directory, std::stoll(entry.file_stem));
    }
    if (folder_clean) {
      (void)RecoveryInstanceFolder::remove_folder(orphan.directory);
    }
  }
  if (!recovered.empty()) {
    statusBar()->showMessage(tr("Recovered %n unsaved document(s) from the last session", nullptr,
                                static_cast<int>(recovered.size())));
  }
  if (failed > 0) {
    show_status_error(tr("%n recovery file(s) could not be opened; see %1", nullptr, failed)
                          .arg(QDir::toNativeSeparators(RecoveryInstanceFolder::recovery_root())));
  }
  return recovered;
}

int MainWindow::discard_orphaned_recovery() {
  int dropped = 0;
  for (const auto& orphan : list_orphaned_recovery()) {
    dropped += static_cast<int>(orphan.entries.size());
    (void)RecoveryInstanceFolder::remove_folder(orphan.directory);
  }
  return dropped;
}

#endif  // Q_OS_WASM

}  // namespace patchy::ui
