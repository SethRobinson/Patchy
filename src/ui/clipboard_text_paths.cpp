#include "ui/clipboard_text_paths.hpp"

#include <QDir>
#include <QUrl>

namespace patchy::ui {
namespace {

bool is_separator(QChar ch) { return ch == QLatin1Char('/') || ch == QLatin1Char('\\'); }

bool has_control_character(QStringView line) {
  for (const auto ch : line) {
    if (ch.unicode() < 0x20U || ch.unicode() == 0x7FU) {
      return true;
    }
  }
  return false;
}

QStringView without_surrounding_quotes(QStringView line) {
  if (line.size() >= 2) {
    const auto first = line.front();
    if ((first == QLatin1Char('"') || first == QLatin1Char('\'')) && line.back() == first) {
      return line.sliced(1, line.size() - 2).trimmed();
    }
  }
  return line;
}

bool is_absolute(const QString& path, ClipboardPathStyle style) {
  if (style == ClipboardPathStyle::Posix) {
    return path.startsWith(QLatin1Char('/'));
  }
  // C:\ or C:/ (a bare "C:" or "C:name" is relative to that drive's current folder).
  if (path.size() >= 3 && path[0].isLetter() && path[0].unicode() < 0x80U && path[1] == QLatin1Char(':') &&
      is_separator(path[2])) {
    return true;
  }
  // \\server\share (or //server/share) and the \\?\ long-path prefix.
  return path.size() >= 3 && is_separator(path[0]) && is_separator(path[1]) && !is_separator(path[2]);
}

QString with_style_separators(QString path, ClipboardPathStyle style) {
  return style == ClipboardPathStyle::Windows ? path.replace(QLatin1Char('/'), QLatin1Char('\\')) : path;
}

}  // namespace

QStringList file_paths_from_clipboard_text(const QString& text, ClipboardPathStyle style) {
  if (text.size() > kClipboardPathTextMaxChars) {
    return {};
  }
  QStringList paths;
  qsizetype lines = 0;
  for (const auto raw_line : QStringView(text).split(QLatin1Char('\n'))) {
    const auto trimmed = raw_line.trimmed();  // also drops the \r of a CRLF line end
    if (trimmed.isEmpty()) {
      continue;
    }
    if (++lines > kClipboardPathTextMaxLines) {
      return {};
    }
    const auto line = without_surrounding_quotes(trimmed);
    if (line.isEmpty() || has_control_character(line)) {
      return {};
    }
    QString path;
    bool from_home = false;
    if (line.startsWith(QStringLiteral("file:"), Qt::CaseInsensitive)) {
      const QUrl url(line.toString());
      if (!url.isValid() || !url.isLocalFile()) {
        return {};
      }
      path = url.toLocalFile();
      // Qt keeps the slash before a drive letter ("/C:/...") outside Windows.
      if (style == ClipboardPathStyle::Windows && path.size() >= 4 && path[0] == QLatin1Char('/') &&
          path[2] == QLatin1Char(':') && is_separator(path[3])) {
        path.remove(0, 1);
      }
    } else {
      path = line.toString();
      if (style == ClipboardPathStyle::Posix && path.startsWith(QStringLiteral("~/"))) {
        path = QDir::homePath() + path.mid(1);
        from_home = true;
      }
    }
    if (!from_home && !is_absolute(path, style)) {
      return {};
    }
    path = with_style_separators(std::move(path), style);
    if (!paths.contains(path)) {
      paths.push_back(path);
    }
  }
  return paths;
}

}  // namespace patchy::ui
