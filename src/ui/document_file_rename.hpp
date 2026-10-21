#pragma once

// Rename File (the document tab's context menu, docs/document-tabs.md): the name
// rules, the on-disk rename that never replaces another file, and the dialog that
// asks for the new name. MainWindow owns what follows a rename (session path, title,
// recent files, recovery sidecar); this file only touches the one file.

#include <QString>

#include <functional>

class QWidget;

namespace patchy::ui {

// "photo.psd" -> {"photo", ".psd"}. The extension is everything from the last dot
// and includes it; a name without one, or whose only dot leads (".hidden"), has none.
struct FileNameParts {
  QString stem;
  QString extension;
};
[[nodiscard]] FileNameParts split_file_name(const QString& file_name);

// Empty when `stem + extension` can name a file in a folder here; otherwise the
// translated reason. Refused everywhere: an empty or blank stem, a name of only dots
// and spaces, `/` or `\`, control characters, the Windows device names (CON, NUL,
// COM1...; they make the file unusable on Windows), and over 255 characters. Windows
// also refuses `< > : " | ? *` and a trailing dot or space, macOS `:`.
[[nodiscard]] QString file_name_problem(const QString& stem, const QString& extension);

enum class FileRenameStatus { Renamed, SourceMissing, TargetExists, Failed };
struct FileRenameResult {
  FileRenameStatus status{FileRenameStatus::Failed};
  // For Failed: the system's reason, already readable ("Access is denied.").
  QString detail;
};

// Renames `from` to `to` (the same folder) without ever replacing an existing file:
// a `to` that exists is TargetExists and both files stay as they were. A case-only
// change of the same file on a case-insensitive file system (`photo.psd` to
// `Photo.psd`) is not a collision; it goes through a unique temporary name beside the
// file. Never std::filesystem::rename or the atomic writer's replacing rename.
[[nodiscard]] FileRenameResult rename_file_without_replacing(const QString& from, const QString& to);

// The Rename File dialog for `current_path`: the stem is editable and selected, the
// extension is fixed text beside it, and name problems show inline while typing. OK
// with an unchanged name closes with nothing done. Otherwise OK calls `attempt` with
// the new absolute path; a non-empty return is shown inline and the dialog stays open
// for another name. Returns true once `attempt` succeeded.
bool request_file_rename(QWidget* parent, const QString& current_path,
                         const std::function<QString(const QString& new_path)>& attempt);

}  // namespace patchy::ui
