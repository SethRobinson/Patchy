#include "ui/document_recovery_coordinator.hpp"

#ifndef Q_OS_WASM

#include "psd/psd_document_io.hpp"
#include "ui/background_workers.hpp"
#include "ui/qt_paths.hpp"

#include <QApplication>
#include <QDateTime>
#include <QEventLoop>
#include <QMetaObject>
#include <QPointer>
#include <QTimer>

#include <exception>
#include <string>
#include <system_error>
#include <utility>

namespace patchy::ui {
namespace {

QString original_path_from_entry(const recovery::RecoveryEntry& entry) {
  if (entry.original_path.empty()) {
    return QString();
  }
  return to_qstring(std::filesystem::path(std::u8string(entry.original_path.begin(), entry.original_path.end())));
}

}  // namespace

DocumentRecoveryCoordinator::DocumentRecoveryCoordinator(RecoveryHost& host, const QString& root, QObject* parent)
    : QObject(parent), host_(host), folder_(std::make_shared<RecoveryInstanceFolder>(root)) {
  timer_ = new QTimer(this);
  timer_->setObjectName(QStringLiteral("documentRecoveryTimer"));
  timer_->setTimerType(Qt::VeryCoarseTimer);
  connect(timer_, &QTimer::timeout, this, [this] {
    if (!host_.recovery_suppressed_for_automation()) {
      write_now(/*wait=*/false);
    }
  });
}

DocumentRecoveryCoordinator::~DocumentRecoveryCoordinator() = default;

void DocumentRecoveryCoordinator::apply_preferences(bool enabled, int interval_minutes) {
  if (!enabled) {
    timer_->stop();
    return;
  }
  int interval_ms = interval_minutes * 60 * 1000;
  // Test hook: a short interval makes the timer observable inside a test's budget.
  if (const auto override_ms = qEnvironmentVariable("PATCHY_RECOVERY_INTERVAL_MS").toInt(); override_ms > 0) {
    interval_ms = override_ms;
  }
  timer_->start(interval_ms);
}

void DocumentRecoveryCoordinator::stop_timer() { timer_->stop(); }

QString DocumentRecoveryCoordinator::directory() const { return folder_->directory_string(); }

std::vector<recovery::RecoveryEntry> DocumentRecoveryCoordinator::list_entries() const {
  if (!folder_->created()) {
    return {};
  }
  return recovery::scan_instance_dir(folder_->directory());
}

std::vector<OrphanedRecoveryFolder> DocumentRecoveryCoordinator::list_orphaned() {
  return RecoveryInstanceFolder::scan_orphaned(RecoveryInstanceFolder::recovery_root());
}

void DocumentRecoveryCoordinator::discard_folder_on_release() {
  timer_->stop();
  folder_->discard_on_release();
}

void DocumentRecoveryCoordinator::discard_folder_for_forced_exit() {
  timer_->stop();
  if (!folder_->created()) {
    return;
  }
  folder_->discard_on_release();
  // The lock file stays open until the process ends (Windows refuses to delete
  // it), so what may survive is a lock-only folder, which the next start sweeps
  // as an empty orphan.
  (void)RecoveryInstanceFolder::remove_folder(folder_->directory());
}

void DocumentRecoveryCoordinator::discard_for_session(std::int64_t session_id) {
  marks_.erase(session_id);
  if (folder_->created()) {
    recovery::remove_entry(folder_->directory(), session_id);
  }
}

QStringList DocumentRecoveryCoordinator::write_now(bool wait) {
  QStringList written;
  if (write_in_flight_ || host_.recovery_busy()) {
    return written;
  }
  auto jobs = std::make_shared<std::vector<RecoveryJob>>(host_.collect_recovery_jobs(marks_));
  if (jobs->empty()) {
    return written;
  }
  if (!folder_->ensure_created()) {
    host_.report_recovery_folder_unavailable(folder_->directory_string());
    return written;
  }
  const auto directory = folder_->directory();
  for (const auto& job : *jobs) {
    written.push_back(to_qstring(recovery::document_path(directory, job.mark.session_id)));
  }
  write_in_flight_ = true;
  auto folder = folder_;
  auto* app = QApplication::instance();
  QPointer<DocumentRecoveryCoordinator> self(this);
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
            self->finish_write(*marks, *errors);
          }
        },
        Qt::QueuedConnection);
  });
  if (wait) {
    while (write_in_flight_) {
      QApplication::processEvents(QEventLoop::AllEvents, 15);
    }
  }
  return written;
}

void DocumentRecoveryCoordinator::finish_write(const std::vector<RecoveryMark>& marks, const QStringList& errors) {
  write_in_flight_ = false;
  for (const auto& mark : marks) {
    if (!host_.modified_session_mark(mark.session_id).has_value()) {
      // Closed or saved while the copy was being written: the copy is stale, and a
      // close or save already removed the previous one, so remove this one too.
      recovery::remove_entry(folder_->directory(), mark.session_id);
      marks_.erase(mark.session_id);
      continue;
    }
    marks_[mark.session_id] = mark;
  }
  if (!errors.isEmpty()) {
    host_.report_recovery_write_error(errors.front());
  }
}

std::vector<std::int64_t> DocumentRecoveryCoordinator::recover_orphaned_documents() {
  std::vector<std::int64_t> recovered;
  int failed = 0;
  int left_in_place = 0;
  for (const auto& orphan : list_orphaned()) {
    bool folder_clean = true;
    for (const auto& entry : orphan.entries) {
      const auto orphan_session_id = std::stoll(entry.file_stem);
      const auto psb_path = recovery::document_path(orphan.directory, orphan_session_id);
      const auto sidecar_path = recovery::sidecar_path(orphan.directory, orphan_session_id);
      std::int64_t session_id = 0;
      if (!host_.open_recovered_document(to_qstring(psb_path), host_.recovered_document_title(entry.title),
                                         original_path_from_entry(entry), &session_id)) {
        ++failed;
        folder_clean = false;
        continue;
      }
      recovered.push_back(session_id);
      // Move the copy under this instance so a second crash is covered too; the
      // session's current state IS the copy, so the timer need not rewrite it. The
      // orphan's files go only once the copy is in place under this instance: until
      // then, and whenever the move fails, the old copy stays where it is (the open
      // session has no mark, so the timer writes a fresh copy when it is next due).
      bool adopted = false;
      if (const auto mark = host_.modified_session_mark(session_id); mark.has_value() && folder_->ensure_created()) {
        const auto& directory = folder_->directory();
        std::error_code error;
        std::filesystem::rename(psb_path, recovery::document_path(directory, session_id), error);
        if (!error) {
          adopted = true;
          std::filesystem::rename(sidecar_path, recovery::sidecar_path(directory, session_id), error);
          if (error) {
            // The sidecar's content is in hand: rewrite it rather than leave the moved
            // copy to recover as untitled next time.
            try {
              recovery::write_sidecar(directory, session_id, entry);
            } catch (const std::exception&) {
              // The PSB is what matters; a missing sidecar only costs the title.
            }
          }
          marks_[session_id] = *mark;
        }
      }
      if (adopted) {
        recovery::remove_entry(orphan.directory, orphan_session_id);
      } else {
        ++left_in_place;
        folder_clean = false;
      }
    }
    if (folder_clean) {
      (void)RecoveryInstanceFolder::remove_folder(orphan.directory);
    }
  }
  host_.report_recovery_result(static_cast<int>(recovered.size()), failed, left_in_place);
  return recovered;
}

int DocumentRecoveryCoordinator::discard_orphaned() {
  int dropped = 0;
  for (const auto& orphan : list_orphaned()) {
    dropped += static_cast<int>(orphan.entries.size());
    (void)RecoveryInstanceFolder::remove_folder(orphan.directory);
  }
  return dropped;
}

}  // namespace patchy::ui

#endif  // Q_OS_WASM
