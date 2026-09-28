#include "ui/app_data_migration.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

#include <algorithm>

namespace patchy::ui::app_data_migration {
namespace {

bool files_are_identical(const QString& a, const QString& b) {
  QFile file_a(a);
  QFile file_b(b);
  if (file_a.size() != file_b.size()) {
    return false;
  }
  if (!file_a.open(QIODevice::ReadOnly) || !file_b.open(QIODevice::ReadOnly)) {
    return false;
  }
  constexpr qint64 kChunk = 1 << 16;
  while (!file_a.atEnd()) {
    if (file_a.read(kChunk) != file_b.read(kChunk)) {
      return false;
    }
  }
  return file_b.atEnd();
}

bool move_file(const QString& from, const QString& to) {
  if (!QDir().mkpath(QFileInfo(to).absolutePath())) {
    return false;
  }
  if (QFile::rename(from, to)) {
    return true;
  }
  // rename fails across volumes (a redirected profile folder); copy, then drop the source.
  return QFile::copy(from, to) && QFile::remove(from);
}

// Removes `dir` when it holds nothing, then repeats for its parents up to and
// including `stop_at` (the legacy organization folder). Anything non-empty stays.
void prune_empty_directories(QString dir, const QString& stop_at) {
  while (true) {
    QDir current(dir);
    if (!current.exists() || !current.isEmpty()) {
      return;
    }
    const auto parent = QFileInfo(dir).absolutePath();
    if (!QDir().rmdir(dir) || QDir::cleanPath(dir) == QDir::cleanPath(stop_at)) {
      return;
    }
    dir = parent;
  }
}

}  // namespace

MigrationResult migrate_app_data_directory(const QString& legacy_dir, const QString& current_dir) {
  MigrationResult result;
  if (legacy_dir.isEmpty() || current_dir.isEmpty() ||
      QDir::cleanPath(legacy_dir) == QDir::cleanPath(current_dir)) {
    return result;
  }
  const QDir legacy(legacy_dir);
  if (!legacy.exists()) {
    return result;
  }
  result.legacy_found = true;

  QDirIterator it(legacy_dir, QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot,
                  QDirIterator::Subdirectories);
  while (it.hasNext()) {
    const auto source = it.next();
    const auto relative = legacy.relativeFilePath(source);
    const auto target = QDir(current_dir).filePath(relative);
    if (QFileInfo::exists(target)) {
      if (files_are_identical(source, target) && QFile::remove(source)) {
        ++result.duplicates_removed;
      } else {
        ++result.conflicts_kept;
      }
      continue;
    }
    if (move_file(source, target)) {
      ++result.files_moved;
    } else {
      ++result.conflicts_kept;
    }
  }

  // Deepest folders first, so a parent is empty by the time it is visited.
  QStringList directories;
  for (QDirIterator dirs(legacy_dir, QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot,
                         QDirIterator::Subdirectories);
       dirs.hasNext();) {
    directories << dirs.next();
  }
  std::sort(directories.begin(), directories.end(),
            [](const QString& a, const QString& b) { return a.size() > b.size(); });
  for (const auto& dir : directories) {
    if (QDir(dir).isEmpty()) {
      QDir().rmdir(dir);
    }
  }
  prune_empty_directories(legacy_dir, QFileInfo(legacy_dir).absolutePath());
  result.completed = !QDir(legacy_dir).exists();
  return result;
}

QString legacy_app_data_directory() {
#ifdef Q_OS_WASM
  return {};
#else
  const auto current_organization = QCoreApplication::organizationName();
  if (current_organization == QString::fromLatin1(kLegacyOrganizationName)) {
    return {};
  }
  QCoreApplication::setOrganizationName(QString::fromLatin1(kLegacyOrganizationName));
  const auto path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  QCoreApplication::setOrganizationName(current_organization);
  return path;
#endif
}

MigrationResult migrate_legacy_app_data() {
#ifdef Q_OS_WASM
  return {};
#else
  const auto legacy = legacy_app_data_directory();
  if (legacy.isEmpty() || !QDir(legacy).exists()) {
    return {};
  }
  return migrate_app_data_directory(
      legacy, QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));
#endif
}

}  // namespace patchy::ui::app_data_migration
