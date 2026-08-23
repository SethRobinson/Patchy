#pragma once

#include <QString>

#include <filesystem>

namespace patchy::ui {

// QString <-> std::filesystem::path for every file that Patchy reads or writes.
//
// toStdU16String -> fs::path converts UTF-16 to the native path encoding on every
// platform (wide on Windows, UTF-8 on POSIX). Never hand a file path to a
// std::filesystem::path parameter through QString::toStdString(): that is UTF-8
// bytes, and MSVC's path(std::string) decodes them with the ANSI code page, so a
// name typed as U+305B U+3059 was written to disk as U+00E3 U+0081 U+203A U+00E3
// U+0081 U+2122 (the August 2026 Save As bug). toStdWString is wrong too: wchar_t is UTF-32 on POSIX and takes the
// locale-dependent conversion.
[[nodiscard]] inline std::filesystem::path to_filesystem_path(const QString& path) {
  return std::filesystem::path(path.toStdU16String());
}

[[nodiscard]] inline QString to_qstring(const std::filesystem::path& path) {
  return QString::fromStdU16String(path.u16string());
}

}  // namespace patchy::ui
