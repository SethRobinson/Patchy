#pragma once

// The automatic document recovery lifecycle (docs/document-recovery.md), owned apart
// from MainWindow: the timer, the per-instance folder, the one-write-at-a-time
// background job, the per-session marks, and the reopen of a crashed instance's
// copies. The coordinator knows nothing about sessions, canvases or the status bar; it
// reaches the window only through RecoveryHost. Not compiled for the web build.

#include <QtGlobal>

#ifndef Q_OS_WASM

#include "core/document.hpp"
#include "core/document_recovery_store.hpp"
#include "ui/document_recovery.hpp"

#include <QObject>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

class QTimer;

namespace patchy::ui {

// The document state a recovery copy holds. `revision` alone is not monotonic (undo
// restores the old value), so the (revision, state id) pair identifies a state.
struct RecoveryMark {
  std::int64_t session_id{0};
  std::int64_t revision{0};
  std::int64_t state_id{0};
};

// One session's snapshot for a background write: the mark the copy will hold, a
// Document copy (copy-on-write pixel storage shared with the live document until its
// next edit; the worker only reads it) and the sidecar fields.
struct RecoveryJob {
  RecoveryMark mark;
  Document snapshot;
  recovery::RecoveryEntry entry;
};

// What the coordinator needs from the owner of the documents. MainWindow implements
// it (main_window_recovery.cpp); the user-facing text stays with the host so its
// translation context is unchanged.
class RecoveryHost {
public:
  virtual ~RecoveryHost() = default;

  // Nothing may be written right now: a modal dialog, the preview-dialog edit lock, or
  // a canvas gesture in progress. A tick during this is skipped, not deferred.
  [[nodiscard]] virtual bool recovery_busy() const = 0;
  // Export, run-script, headless and hidden-connector instances never write on the
  // timer (their documents are the automation's); an explicit write_now still does.
  [[nodiscard]] virtual bool recovery_suppressed_for_automation() const = 0;
  // One job per modified session whose current state differs from the mark its copy
  // holds (`marks[session_id]`, absent when it has no copy).
  [[nodiscard]] virtual std::vector<RecoveryJob> collect_recovery_jobs(
      const std::unordered_map<std::int64_t, RecoveryMark>& marks) = 0;
  // The session's current state while it is open and modified; nullopt once it was
  // closed or saved (its copy is then stale).
  [[nodiscard]] virtual std::optional<RecoveryMark> modified_session_mark(std::int64_t session_id) = 0;
  // Opens one recovery copy as a new modified session titled `title`, with its path
  // set to `original_path` when not empty. `session_id` receives the new session.
  virtual bool open_recovered_document(const QString& psb_path, const QString& title, const QString& original_path,
                                       std::int64_t* session_id) = 0;
  // "<title> (Recovered)", with the untitled fallback when `title` is empty.
  [[nodiscard]] virtual QString recovered_document_title(const std::string& title) = 0;

  virtual void report_recovery_folder_unavailable(const QString& directory) = 0;
  virtual void report_recovery_write_error(const QString& error) = 0;
  // After a reopen pass: how many copies opened, how many failed to open, and how many
  // opened but could not be moved under this instance (their copies stay put).
  virtual void report_recovery_result(int recovered, int failed_to_open, int left_in_place) = 0;
};

class DocumentRecoveryCoordinator : public QObject {
public:
  // `root` is the recovery root (RecoveryInstanceFolder::recovery_root()). The timer is
  // a child of this object named "documentRecoveryTimer" and starts stopped; call
  // apply_preferences to arm it.
  DocumentRecoveryCoordinator(RecoveryHost& host, const QString& root, QObject* parent);
  ~DocumentRecoveryCoordinator() override;

  // Arms or stops the timer. `PATCHY_RECOVERY_INTERVAL_MS` overrides the interval
  // (tests).
  void apply_preferences(bool enabled, int interval_minutes);
  void stop_timer();
  [[nodiscard]] bool write_in_flight() const noexcept { return write_in_flight_; }

  [[nodiscard]] QString directory() const;
  [[nodiscard]] std::vector<recovery::RecoveryEntry> list_entries() const;
  [[nodiscard]] static std::vector<OrphanedRecoveryFolder> list_orphaned();

  // Writes a copy of every modified session whose state changed since its last copy,
  // on a tracked background worker; returns the copy paths it will write. `wait`
  // pumps events until the write finished. Nothing is written while busy or while a
  // write is in flight.
  QStringList write_now(bool wait);
  // A save or a close: the copy and its mark go.
  void discard_for_session(std::int64_t session_id);

  // A normal quit: the folder is deleted by whichever owner releases it last, this
  // object or a background write still running.
  void discard_folder_on_release();
  // Forced exit (main.cpp: a worker still blocked after the bounded quit wait): stops
  // the timer and deletes the folder now, since destructors will not run.
  void discard_folder_for_forced_exit();

  // Reopens every copy a crashed instance left and moves it under this instance;
  // returns the new session ids. See docs/document-recovery.md.
  std::vector<std::int64_t> recover_orphaned_documents();
  // Deletes every orphaned recovery folder; returns how many documents were dropped.
  int discard_orphaned();

private:
  void finish_write(const std::vector<RecoveryMark>& marks, const QStringList& errors);

  RecoveryHost& host_;
  std::shared_ptr<RecoveryInstanceFolder> folder_;
  QTimer* timer_{nullptr};
  bool write_in_flight_{false};
  // Per session id: the (revision, state id) pair its current recovery copy holds.
  std::unordered_map<std::int64_t, RecoveryMark> marks_;
};

}  // namespace patchy::ui

#endif  // Q_OS_WASM
