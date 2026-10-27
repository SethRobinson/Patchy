#pragma once

#include <QString>
#include <QStringList>

// File paths copied as text (docs/clipboard.md, "File paths copied as text"):
// Edit > Paste opens them and Paste in Place adds them as layers when the
// clipboard holds no image, layer payload, SVG or file URLs.
namespace patchy::ui {

enum class ClipboardPathStyle {
  Windows,  // drive (C:\...) and UNC (\\server\share\...) paths
  Posix,    // /... paths, ~/ expanded to the home folder
#ifdef Q_OS_WIN
  Native = Windows,
#else
  Native = Posix,
#endif
};

// Limits past which clipboard text is never read as paths.
inline constexpr qsizetype kClipboardPathTextMaxChars = 32 * 1024;
inline constexpr qsizetype kClipboardPathTextMaxLines = 32;

// The absolute paths `text` names, one per non-empty line, in order and without
// duplicates, with native separators. A line may be wrapped in one pair of double
// or single quotes (Windows "Copy as path") or be a file:// URL. Returns an empty
// list unless every line is such a path: relative paths, other URLs, control
// characters, too many lines or too much text mean the text is not a path list.
// Nothing here touches the file system.
[[nodiscard]] QStringList file_paths_from_clipboard_text(const QString& text,
                                                         ClipboardPathStyle style = ClipboardPathStyle::Native);

}  // namespace patchy::ui
