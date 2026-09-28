#pragma once

// The per-instance folder of the automatic document recovery store and the
// liveness lock that tells a crashed instance's folder from a running one
// (docs/document-recovery.md). Not compiled for the web build: MEMFS is recreated
// on every page load, so there is nothing durable to recover from.

#include <QtGlobal>

#ifndef Q_OS_WASM

#include "core/document_recovery_store.hpp"

#include <QLockFile>
#include <QString>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>

namespace patchy::ui {

struct OrphanedRecoveryFolder {
  std::filesystem::path directory;
  std::vector<recovery::RecoveryEntry> entries;
};

// One running Patchy instance's recovery folder, `<root>/<pid>-<start ms>/`, held
// open by a QLockFile so another instance can tell whether its owner is still
// alive (QLockFile stores the pid and checks that the process exists; the age
// heuristic is disabled). The folder is created lazily by the first write, so an
// instance that never wrote leaves nothing behind. It is shared by the window and
// by any in-flight background write; the owner that releases it last removes the
// folder when discard_on_release() was requested (a normal quit), and leaves it for
// the next launch otherwise (a crash never runs the destructor at all).
class RecoveryInstanceFolder {
public:
  // `PATCHY_RECOVERY_DIR` when set (tests, automation), else
  // <AppDataLocation>/AutoRecover.
  [[nodiscard]] static QString recovery_root();

  explicit RecoveryInstanceFolder(QString root);
  ~RecoveryInstanceFolder();
  RecoveryInstanceFolder(const RecoveryInstanceFolder&) = delete;
  RecoveryInstanceFolder& operator=(const RecoveryInstanceFolder&) = delete;

  [[nodiscard]] const std::filesystem::path& directory() const noexcept { return directory_; }
  [[nodiscard]] QString directory_string() const;
  // Creates the folder and takes the lock; false when either fails (the write
  // that needed it reports the error). Idempotent once it succeeded.
  bool ensure_created();
  [[nodiscard]] bool created() const noexcept { return locked_; }
  void discard_on_release() noexcept { discard_ = true; }

  // Instance folders under `root` whose owner is gone: no lock file, or a lock
  // whose process no longer runs. A live instance's folder is never listed. Empty
  // orphan folders (nothing was ever written) are removed on the way.
  [[nodiscard]] static std::vector<OrphanedRecoveryFolder> scan_orphaned(const QString& root);
  [[nodiscard]] static bool remove_folder(const std::filesystem::path& directory) noexcept;

private:
  std::filesystem::path directory_;
  std::unique_ptr<QLockFile> lock_;
  bool locked_{false};
  bool discard_{false};
};

}  // namespace patchy::ui

#endif  // Q_OS_WASM
