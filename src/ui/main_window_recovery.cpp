// Automatic document recovery (docs/document-recovery.md): MainWindow's half. The
// lifecycle itself (timer, folder, marks, background write, orphan reopen) is
// DocumentRecoveryCoordinator; this TU implements its RecoveryHost over the session
// list, owns the user-facing text, and forwards the public recovery members.

#include "ui/main_window.hpp"
#include "ui/main_window_shared.hpp"

#include "core/document_recovery_store.hpp"
#include "psd/psd_document_io.hpp"
#include "support/path_utils.hpp"
#include "ui/app_settings.hpp"
#include "ui/background_workers.hpp"
#include "ui/canvas_widget.hpp"
#include "ui/document_recovery.hpp"
#include "ui/document_recovery_coordinator.hpp"
#include "ui/qt_paths.hpp"

#include <QApplication>
#include <QDir>
#include <QStatusBar>
#include <QTextEdit>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace patchy::ui {

bool MainWindow::any_canvas_interaction_active() const {
  for (const auto& target_session : sessions_) {
    const auto* canvas = target_session != nullptr ? target_session->canvas : nullptr;
    if (canvas != nullptr &&
        (canvas->pointer_gesture_active() || canvas->free_transform_active() || canvas->warp_transform_active() ||
         canvas->path_transform_active() || canvas->crop_session_has_changes() ||
         canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor")) != nullptr)) {
      return true;
    }
  }
  return false;
}

#ifndef Q_OS_WASM

// The window's side of the recovery lifecycle: what the coordinator asks of the
// document owner (RecoveryHost), the user-facing text, and the public forwarders.
class MainWindow::RecoveryHostAdapter final : public RecoveryHost {
public:
  explicit RecoveryHostAdapter(MainWindow& window) noexcept : window_(window) {}

  [[nodiscard]] bool recovery_busy() const override { return window_.recovery_busy(); }
  [[nodiscard]] bool recovery_suppressed_for_automation() const override { return window_.cli_automation_mode_; }

  [[nodiscard]] std::vector<RecoveryJob> collect_recovery_jobs(
      const std::unordered_map<std::int64_t, RecoveryMark>& marks) override {
    std::vector<RecoveryJob> jobs;
    for (const auto& target_session : window_.sessions_) {
      if (target_session == nullptr || !window_.session_is_modified(*target_session)) {
        continue;
      }
      const auto mark = marks.find(target_session->session_id);
      if (mark != marks.end() && mark->second.revision == target_session->revision &&
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
      jobs.push_back(std::move(job));
    }
    return jobs;
  }

  [[nodiscard]] std::optional<RecoveryMark> modified_session_mark(std::int64_t session_id) override {
    const auto* target_session = window_.session_with_id(session_id);
    if (target_session == nullptr || !window_.session_is_modified(*target_session)) {
      return std::nullopt;
    }
    return RecoveryMark{session_id, target_session->revision, target_session->current_state_id};
  }

  bool open_recovered_document(const QString& psb_path, const QString& title, const QString& original_path,
                               std::int64_t* session_id) override {
    return window_.open_recovered_document(psb_path, title, original_path, session_id);
  }

  [[nodiscard]] QString recovered_document_title(const std::string& title) override {
    const auto name = title.empty() ? MainWindow::tr("Untitled") : QString::fromStdString(title);
    return MainWindow::tr("%1 (Recovered)").arg(name);
  }

  void report_recovery_folder_unavailable(const QString& directory) override {
    window_.show_status_error(
        MainWindow::tr("Could not save recovery information: %1").arg(QDir::toNativeSeparators(directory)));
  }

  void report_recovery_write_error(const QString& error) override {
    window_.show_status_error(MainWindow::tr("Could not save recovery information: %1").arg(error));
  }

  void report_recovery_result(int recovered, int failed_to_open, int left_in_place) override {
    if (recovered > 0) {
      window_.statusBar()->showMessage(
          MainWindow::tr("Recovered %n unsaved document(s) from the last session", nullptr, recovered));
    }
    const auto root = QDir::toNativeSeparators(RecoveryInstanceFolder::recovery_root());
    if (failed_to_open > 0) {
      window_.show_status_error(
          MainWindow::tr("%n recovery file(s) could not be opened; see %1", nullptr, failed_to_open).arg(root));
    } else if (left_in_place > 0) {
      window_.show_status_error(
          MainWindow::tr("%n recovered document(s) could not be moved to this session's recovery folder; "
                         "the copies stay in %1",
                         nullptr, left_in_place)
              .arg(root));
    }
  }

private:
  MainWindow& window_;
};

void MainWindow::start_document_recovery() {
  recovery_host_ = std::make_shared<RecoveryHostAdapter>(*this);
  recovery_coordinator_ =
      new DocumentRecoveryCoordinator(*recovery_host_, RecoveryInstanceFolder::recovery_root(), this);
  apply_recovery_preferences();
}

void MainWindow::apply_recovery_preferences() {
  if (recovery_coordinator_ == nullptr) {
    return;
  }
  recovery_coordinator_->apply_preferences(stored_recovery_enabled(), stored_recovery_interval_minutes());
}

bool MainWindow::recovery_busy() const {
  return QApplication::activeModalWidget() != nullptr || preview_dialog_edit_locked() ||
         any_canvas_interaction_active();
}

QString MainWindow::recovery_directory() const {
  return recovery_coordinator_ != nullptr ? recovery_coordinator_->directory() : QString();
}

std::vector<recovery::RecoveryEntry> MainWindow::list_recovery_entries() const {
  return recovery_coordinator_ != nullptr ? recovery_coordinator_->list_entries()
                                          : std::vector<recovery::RecoveryEntry>{};
}

std::vector<OrphanedRecoveryFolder> MainWindow::list_orphaned_recovery() const {
  return DocumentRecoveryCoordinator::list_orphaned();
}

void MainWindow::discard_recovery_folder_for_forced_exit() {
  if (recovery_coordinator_ != nullptr) {
    recovery_coordinator_->discard_folder_for_forced_exit();
  }
}

void MainWindow::discard_recovery_for_session(std::int64_t session_id) {
  if (recovery_coordinator_ != nullptr) {
    recovery_coordinator_->discard_for_session(session_id);
  }
}

QStringList MainWindow::write_recovery_now(bool wait) {
  return recovery_coordinator_ != nullptr ? recovery_coordinator_->write_now(wait) : QStringList{};
}

std::vector<std::int64_t> MainWindow::recover_orphaned_documents() {
  return recovery_coordinator_ != nullptr ? recovery_coordinator_->recover_orphaned_documents()
                                          : std::vector<std::int64_t>{};
}

int MainWindow::discard_orphaned_recovery() {
  return recovery_coordinator_ != nullptr ? recovery_coordinator_->discard_orphaned() : 0;
}

#endif  // Q_OS_WASM

}  // namespace patchy::ui
