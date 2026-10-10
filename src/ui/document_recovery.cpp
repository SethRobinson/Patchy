#include "ui/document_recovery.hpp"

#ifndef Q_OS_WASM

#include "support/path_utils.hpp"
#include "ui/qt_paths.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QStandardPaths>

#include <algorithm>
#include <system_error>
#include <vector>

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
    // symlink_status: a link or junction named like an instance folder is not one,
    // and following it would scan (and sweep) whatever it points at.
    const auto status = item.symlink_status(error);
    if (error || !std::filesystem::is_directory(status) || !is_instance_folder_name(item.path().filename())) {
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
      // Nothing to recover: an instance that never wrote, or one whose entries were
      // all removed. remove_folder only deletes a folder holding Patchy's own files,
      // so a date-named folder of someone else's under a custom root stays.
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

bool RecoveryInstanceFolder::is_instance_folder_name(const std::filesystem::path& name) noexcept {
  // <pid>-<msecs>, as the constructor names it.
  const auto text = name.native();
  const auto dash = text.find(static_cast<std::filesystem::path::value_type>('-'));
  if (dash == 0 || dash == std::filesystem::path::string_type::npos || dash + 1 == text.size()) {
    return false;
  }
  for (std::size_t i = 0; i < text.size(); ++i) {
    if (i != dash && (text[i] < '0' || text[i] > '9')) {
      return false;
    }
  }
  return true;
}

bool RecoveryInstanceFolder::remove_folder(const std::filesystem::path& directory) noexcept {
  if (directory.empty() || !directory.is_absolute() || !is_instance_folder_name(directory.filename())) {
    return false;
  }
  std::error_code error;
  // The folder itself, never through a link or junction standing in its place.
  const auto status = std::filesystem::symlink_status(directory, error);
  if (error || !std::filesystem::is_directory(status)) {
    return false;
  }
  // Ownership is proven by the contents, not the name: every entry must be a regular
  // file the store or the lock wrote. Anything else (a subfolder, a link, a photo in a
  // "2026-10" folder under a mistaken PATCHY_RECOVERY_DIR) makes the folder someone
  // else's, and nothing in it is touched. An unreadable folder is not disposable.
  std::filesystem::directory_iterator iterator(directory, error);
  if (error) {
    return false;
  }
  std::vector<std::filesystem::path> owned;
  for (const auto& item : iterator) {
    const auto item_status = item.symlink_status(error);
    if (error || !std::filesystem::is_regular_file(item_status)) {
      return false;
    }
    const auto name = path_to_utf8(item.path().filename());
    if (name != kLockFileName && !recovery::is_store_file_name(name)) {
      return false;
    }
    owned.push_back(item.path());
  }
  // Non-recursive by construction: the files one by one, then the empty folder. A file
  // that will not go (the forced-exit path still holds its own lock open on Windows)
  // does not stop the others: the session copies must leave even when the lock stays,
  // or the next launch would recover them again. That lock-only folder is swept then.
  bool removed_all = true;
  for (const auto& path : owned) {
    std::filesystem::remove(path, error);
    removed_all = removed_all && !error;
  }
  if (!removed_all) {
    return false;
  }
  std::filesystem::remove(directory, error);
  return !error;
}

}  // namespace patchy::ui

#endif  // Q_OS_WASM
