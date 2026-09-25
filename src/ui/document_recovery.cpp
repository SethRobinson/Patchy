#include "ui/document_recovery.hpp"

#ifndef Q_OS_WASM

#include "ui/qt_paths.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QStandardPaths>

#include <algorithm>
#include <system_error>

namespace patchy::ui {
namespace {

constexpr auto kLockFileName = "lock";

std::unique_ptr<QLockFile> make_lock(const std::filesystem::path& directory) {
  auto lock = std::make_unique<QLockFile>(QDir(to_qstring(directory)).filePath(QString::fromLatin1(kLockFileName)));
  // Liveness only: the default 30 s age heuristic would call a long-running live
  // instance's lock stale and let a second instance recover its documents out from
  // under it.
  lock->setStaleLockTime(0);
  return lock;
}

}  // namespace

QString RecoveryInstanceFolder::recovery_root() {
  const auto override_dir = qEnvironmentVariable("PATCHY_RECOVERY_DIR");
  if (!override_dir.isEmpty()) {
    return QDir(override_dir).absolutePath();
  }
  return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath(QStringLiteral("AutoRecover"));
}

RecoveryInstanceFolder::RecoveryInstanceFolder(QString root) {
  const auto name = QStringLiteral("%1-%2")
                        .arg(QCoreApplication::applicationPid())
                        .arg(QDateTime::currentMSecsSinceEpoch());
  directory_ = to_filesystem_path(QDir(root).filePath(name));
}

RecoveryInstanceFolder::~RecoveryInstanceFolder() {
  if (lock_ != nullptr) {
    lock_->unlock();
    lock_.reset();
  }
  if (discard_ && locked_) {
    (void)remove_folder(directory_);
  }
}

QString RecoveryInstanceFolder::directory_string() const { return to_qstring(directory_); }

bool RecoveryInstanceFolder::ensure_created() {
  if (locked_) {
    return true;
  }
  std::error_code error;
  std::filesystem::create_directories(directory_, error);
  if (error) {
    return false;
  }
  lock_ = make_lock(directory_);
  locked_ = lock_->tryLock(0);
  if (!locked_) {
    lock_.reset();
  }
  return locked_;
}

std::vector<OrphanedRecoveryFolder> RecoveryInstanceFolder::scan_orphaned(const QString& root) {
  std::vector<OrphanedRecoveryFolder> orphans;
  std::error_code error;
  const auto root_path = to_filesystem_path(root);
  std::filesystem::directory_iterator iterator(root_path, error);
  if (error) {
    return orphans;
  }
  for (const auto& item : iterator) {
    if (!item.is_directory(error) || error) {
      continue;
    }
    {
      // tryLock succeeds when no instance holds the lock: the file is missing, or
      // its pid is dead (QLockFile removes such a stale lock itself). A live owner
      // makes it fail, and that folder is skipped.
      auto lock = make_lock(item.path());
      if (!lock->tryLock(0)) {
        continue;
      }
      lock->unlock();
    }
    auto entries = recovery::scan_instance_dir(item.path());
    if (entries.empty()) {
      (void)remove_folder(item.path());
      continue;
    }
    orphans.push_back(OrphanedRecoveryFolder{item.path(), std::move(entries)});
  }
  std::sort(orphans.begin(), orphans.end(), [](const OrphanedRecoveryFolder& a, const OrphanedRecoveryFolder& b) {
    return a.directory < b.directory;
  });
  return orphans;
}

bool RecoveryInstanceFolder::remove_folder(const std::filesystem::path& directory) noexcept {
  std::error_code error;
  std::filesystem::remove_all(directory, error);
  return !error;
}

}  // namespace patchy::ui

#endif  // Q_OS_WASM
