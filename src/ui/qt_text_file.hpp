#pragma once

// Replacement save for small Qt-side text files (scripts, `patchy.io.writeTextFile`):
// QSaveFile writes a sibling temporary and renames it over the target, so a failed or
// interrupted write leaves the previous file intact (AGENTS.md: never truncate a
// user's file in place). Text mode, like QFile with QIODevice::Text: a line feed becomes
// the platform line ending.

#include <QByteArray>
#include <QIODevice>
#include <QSaveFile>
#include <QString>

namespace patchy::ui {

// False when the temporary cannot be created, the write fails, or the commit (the
// rename) fails; `error`, when given, receives QSaveFile's description. The target is
// untouched on every failure path.
[[nodiscard]] inline bool save_text_file_atomically(const QString& path, const QByteArray& bytes,
                                                    QString* error = nullptr) {
  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
    if (error != nullptr) {
      *error = file.errorString();
    }
    return false;
  }
  const bool written = file.write(bytes) == bytes.size() && file.error() == QFileDevice::NoError;
  if (!written || !file.commit()) {
    if (error != nullptr) {
      *error = file.errorString();
    }
    file.cancelWriting();
    return false;
  }
  return true;
}

}  // namespace patchy::ui
