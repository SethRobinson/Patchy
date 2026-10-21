#include "ui/appimage_menu_entry.hpp"

#include "support/atomic_file_write.hpp"
#include "ui/appimage_environment.hpp"
#include "ui/qt_paths.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QProcess>
#include <QStandardPaths>

#include <cstddef>
#include <cstdint>
#include <exception>
#include <span>

namespace patchy::ui {

namespace {

class AppImageMenuEntryStrings {
  Q_DECLARE_TR_FUNCTIONS(AppImageMenuEntry)
};

constexpr qint64 kMaxDesktopFileBytes = 256 * 1024;
constexpr qint64 kMaxIconBytes = 16 * 1024 * 1024;
constexpr char kDesktopEntryGroup[] = "[Desktop Entry]";

QString desktop_file_name() {
  return QString::fromLatin1(kAppImageMenuEntryId) + QStringLiteral(".desktop");
}

QString applications_dir(const QString& share) {
  return QDir(share).filePath(QStringLiteral("applications"));
}

std::optional<QByteArray> read_small_file(const QString& path, qint64 limit) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly) || file.size() > limit) {
    return std::nullopt;
  }
  return file.readAll();
}

// The unlocalized keys of the [Desktop Entry] group, values with the string escapes
// (\s \n \t \r \\) resolved.
QHash<QString, QString> desktop_entry_values(const QByteArray& bytes) {
  QHash<QString, QString> values;
  bool in_entry = false;
  for (const auto& raw_line : bytes.split('\n')) {
    const auto line = QString::fromUtf8(raw_line).trimmed();
    if (line.startsWith(QLatin1Char('['))) {
      in_entry = line == QLatin1String(kDesktopEntryGroup);
      continue;
    }
    if (!in_entry || line.startsWith(QLatin1Char('#'))) {
      continue;
    }
    const auto equals = line.indexOf(QLatin1Char('='));
    if (equals <= 0) {
      continue;
    }
    const auto key = line.left(equals).trimmed();
    if (key.contains(QLatin1Char('[')) || values.contains(key)) {
      continue;
    }
    const auto raw = line.mid(equals + 1).trimmed();
    QString value;
    for (qsizetype i = 0; i < raw.size(); ++i) {
      if (raw[i] == QLatin1Char('\\') && i + 1 < raw.size()) {
        const auto next = raw[++i];
        value += next == QLatin1Char('s')   ? QChar(QLatin1Char(' '))
                 : next == QLatin1Char('n') ? QChar(QLatin1Char('\n'))
                 : next == QLatin1Char('t') ? QChar(QLatin1Char('\t'))
                 : next == QLatin1Char('r') ? QChar(QLatin1Char('\r'))
                                            : next;
      } else {
        value += raw[i];
      }
    }
    values.insert(key, value);
  }
  return values;
}

// Splits an Exec value (string escapes already resolved) into its arguments, honoring
// the specification's double quotes and their backslash escapes.
QStringList exec_arguments(const QString& exec) {
  QStringList arguments;
  QString current;
  bool quoted = false;
  bool has_argument = false;
  for (qsizetype i = 0; i < exec.size(); ++i) {
    const auto c = exec[i];
    if (quoted) {
      if (c == QLatin1Char('\\') && i + 1 < exec.size()) {
        current += exec[++i];
      } else if (c == QLatin1Char('"')) {
        quoted = false;
      } else {
        current += c;
      }
    } else if (c == QLatin1Char('"')) {
      quoted = true;
      has_argument = true;
    } else if (c.isSpace()) {
      if (has_argument) {
        arguments.push_back(current);
        current.clear();
        has_argument = false;
      }
    } else {
      current += c;
      has_argument = true;
    }
  }
  if (has_argument) {
    arguments.push_back(current);
  }
  return arguments;
}

// Exec's value with its first argument (the program) replaced; the rest (%F) is kept.
QString exec_with_program(const QString& raw_value, const QString& program_argument) {
  QString rest;
  const auto trimmed = raw_value.trimmed();
  if (trimmed.startsWith(QLatin1Char('"'))) {
    for (qsizetype i = 1; i < trimmed.size(); ++i) {
      if (trimmed[i] == QLatin1Char('\\')) {
        ++i;
      } else if (trimmed[i] == QLatin1Char('"')) {
        rest = trimmed.mid(i + 1);
        break;
      }
    }
  } else {
    const auto space = trimmed.indexOf(QLatin1Char(' '));
    rest = space < 0 ? QString() : trimmed.mid(space);
  }
  return program_argument + rest;
}

QString desktop_string_value(const QString& text) {
  QString escaped = text;
  escaped.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
  return escaped;
}

bool has_control_characters(const QString& text) {
  for (const auto c : text) {
    if (c.unicode() < 0x20 || c.unicode() == 0x7f) {
      return true;
    }
  }
  return false;
}

bool same_file_bytes(const QString& path, const QByteArray& bytes) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly) || file.size() != bytes.size()) {
    return false;
  }
  return file.readAll() == bytes;
}

// Writes `bytes` to `path` through the atomic writer unless the file already holds them.
bool write_if_changed(const QString& path, const QByteArray& bytes, bool& changed, QString& error) {
  if (same_file_bytes(path, bytes)) {
    return true;
  }
  if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
    error = AppImageMenuEntryStrings::tr("Could not create the folder for %1").arg(QDir::toNativeSeparators(path));
    return false;
  }
  try {
    const std::span<const std::uint8_t> span(reinterpret_cast<const std::uint8_t*>(bytes.constData()),
                                             static_cast<std::size_t>(bytes.size()));
    write_file_bytes_atomically(to_filesystem_path(path), span, "open", "write");
  } catch (const std::exception&) {
    error = AppImageMenuEntryStrings::tr("Could not write %1").arg(QDir::toNativeSeparators(path));
    return false;
  }
  changed = true;
  return true;
}

bool written_by_patchy(const QByteArray& desktop_entry) {
  return desktop_entry_values(desktop_entry).value(QString::fromLatin1(kAppImageMenuEntryMarkerKey)) ==
         QStringLiteral("true");
}

AppImageMenuEntryResult write_entry(const AppImageMenuEntryPaths& paths) {
  AppImageMenuEntryResult result;
  const auto entry = build_appimage_desktop_entry(paths, &result.error);
  if (!entry) {
    return result;
  }
  const auto names = appimage_menu_entry_icon_names();
  const auto targets = paths.icon_files();
  bool copied_icon = false;
  for (qsizetype i = 0; i < names.size(); ++i) {
    const auto icon = read_small_file(QDir(paths.bundled_share).filePath(names[i]), kMaxIconBytes);
    if (!icon) {
      continue;  // a size this release does not ship
    }
    if (!write_if_changed(targets[i], *icon, result.changed, result.error)) {
      return result;
    }
    copied_icon = true;
  }
  if (!copied_icon) {
    result.error = AppImageMenuEntryStrings::tr("This AppImage contains no Patchy icons");
    return result;
  }
  if (!write_if_changed(paths.desktop_file(), *entry, result.changed, result.error)) {
    return result;
  }
  result.ok = true;
  return result;
}

}  // namespace

QString AppImageMenuEntryPaths::desktop_file() const {
  return QDir(applications_dir(data_home)).filePath(desktop_file_name());
}

QStringList AppImageMenuEntryPaths::icon_files() const {
  QStringList files;
  for (const auto& name : appimage_menu_entry_icon_names()) {
    files.push_back(QDir(data_home).filePath(name));
  }
  return files;
}

QStringList appimage_menu_entry_icon_names() {
  // The sizes CMake installs (packaging/linux/icons) plus the scalable logo.
  static constexpr int kSizes[] = {16, 32, 48, 64, 128, 256, 512};
  QStringList names;
  const auto png = QString::fromLatin1(kAppImageMenuEntryId) + QStringLiteral(".png");
  for (const int size : kSizes) {
    names.push_back(QStringLiteral("icons/hicolor/%1x%1/apps/").arg(size) + png);
  }
  names.push_back(QStringLiteral("icons/hicolor/scalable/apps/") + QString::fromLatin1(kAppImageMenuEntryId) +
                  QStringLiteral(".svg"));
  return names;
}

QString xdg_data_home() {
  const auto configured = qEnvironmentVariable("XDG_DATA_HOME");
  if (!configured.isEmpty() && QDir::isAbsolutePath(configured)) {
    return QDir::cleanPath(configured);
  }
  return QDir(QDir::homePath()).filePath(QStringLiteral(".local/share"));
}

QStringList xdg_data_dirs() {
  QStringList dirs;
  for (const auto& dir : qEnvironmentVariable("XDG_DATA_DIRS").split(QLatin1Char(':'), Qt::SkipEmptyParts)) {
    if (QDir::isAbsolutePath(dir)) {
      dirs.push_back(QDir::cleanPath(dir));
    }
  }
  if (dirs.isEmpty()) {
    dirs = {QStringLiteral("/usr/local/share"), QStringLiteral("/usr/share")};
  }
  return dirs;
}

AppImageMenuEntryPaths resolve_appimage_menu_entry_paths() {
  AppImageMenuEntryPaths paths;
  paths.appimage_file = appimage_file_path();
  if (!paths.appimage_file.isEmpty()) {
    paths.bundled_share = QDir(qEnvironmentVariable("APPDIR")).filePath(QStringLiteral("usr/share"));
  }
  paths.data_home = xdg_data_home();
  paths.data_dirs = xdg_data_dirs();
  return paths;
}

bool desktop_entry_starts_patchy_appimage(const QByteArray& desktop_entry) {
  const auto values = desktop_entry_values(desktop_entry);
  QString appimage;
  for (const auto& argument : exec_arguments(values.value(QStringLiteral("Exec")))) {
    if (argument.endsWith(QStringLiteral(".appimage"), Qt::CaseInsensitive)) {
      appimage = argument;
      break;
    }
  }
  if (appimage.isEmpty()) {
    return false;
  }
  return values.value(QStringLiteral("Icon")).contains(QString::fromLatin1(kAppImageMenuEntryId), Qt::CaseInsensitive) ||
         values.value(QStringLiteral("Name")).trimmed().compare(QStringLiteral("Patchy"), Qt::CaseInsensitive) == 0 ||
         QFileInfo(appimage).fileName().contains(QStringLiteral("patchy"), Qt::CaseInsensitive);
}

QString appimage_menu_entry_provided_by(const AppImageMenuEntryPaths& paths) {
  const auto own = paths.desktop_file();
  if (QFileInfo::exists(own)) {
    const auto bytes = read_small_file(own, kMaxDesktopFileBytes);
    return bytes && written_by_patchy(*bytes) ? QString() : own;
  }
  // AppImageLauncher, Gear Lever and similar tools write their entries here under their
  // own names.
  const QDir user_applications(applications_dir(paths.data_home));
  for (const auto& name : user_applications.entryList({QStringLiteral("*.desktop")}, QDir::Files, QDir::Name)) {
    const auto path = user_applications.filePath(name);
    const auto bytes = read_small_file(path, kMaxDesktopFileBytes);
    if (bytes && desktop_entry_starts_patchy_appimage(*bytes)) {
      return path;
    }
  }
  // A packaged Patchy (the Flatpak's export, a distribution package) already gives the
  // window its icon; an entry in the user's folder would hide that one.
  for (const auto& dir : paths.data_dirs) {
    const auto packaged = QDir(applications_dir(dir)).filePath(desktop_file_name());
    if (QFileInfo::exists(packaged)) {
      return packaged;
    }
  }
  return {};
}

AppImageMenuEntryState appimage_menu_entry_state(const AppImageMenuEntryPaths& paths) {
  if (paths.appimage_file.isEmpty()) {
    return AppImageMenuEntryState::Unavailable;
  }
  if (!appimage_menu_entry_provided_by(paths).isEmpty()) {
    return AppImageMenuEntryState::AddedElsewhere;
  }
  return QFileInfo::exists(paths.desktop_file()) ? AppImageMenuEntryState::Added : AppImageMenuEntryState::NotAdded;
}

bool appimage_menu_entry_offer_session_allowed() {
  if (!qEnvironmentVariableIsEmpty("CI")) {
    return false;
  }
  return !qEnvironmentVariableIsEmpty("XDG_CURRENT_DESKTOP") || !qEnvironmentVariableIsEmpty("DESKTOP_SESSION");
}

QString desktop_entry_exec_argument(const QString& path) {
  // Inside double quotes the specification reserves `"`, `` ` ``, `$` and `\`, each
  // escaped with a backslash; a literal percent sign is `%%`. The value is then a
  // string, whose own escape doubles every backslash once more.
  QString quoted;
  for (const auto c : path) {
    if (c == QLatin1Char('"') || c == QLatin1Char('`') || c == QLatin1Char('$') || c == QLatin1Char('\\')) {
      quoted += QLatin1Char('\\');
    }
    quoted += c;
  }
  quoted.replace(QLatin1Char('%'), QStringLiteral("%%"));
  return desktop_string_value(QLatin1Char('"') + quoted + QLatin1Char('"'));
}

std::optional<QByteArray> build_appimage_desktop_entry(const AppImageMenuEntryPaths& paths, QString* error) {
  const auto fail = [error](const QString& text) -> std::optional<QByteArray> {
    if (error != nullptr) {
      *error = text;
    }
    return std::nullopt;
  };
  if (paths.appimage_file.isEmpty() || !QDir::isAbsolutePath(paths.appimage_file)) {
    return fail(AppImageMenuEntryStrings::tr("Patchy is not running from an AppImage"));
  }
  if (has_control_characters(paths.appimage_file)) {
    return fail(AppImageMenuEntryStrings::tr("The AppImage's file name cannot be used in a menu entry"));
  }
  const auto shipped =
      read_small_file(QDir(applications_dir(paths.bundled_share)).filePath(desktop_file_name()), kMaxDesktopFileBytes);
  if (!shipped) {
    return fail(AppImageMenuEntryStrings::tr("This AppImage contains no Patchy desktop file"));
  }
  const auto program = desktop_entry_exec_argument(paths.appimage_file);
  QStringList lines;
  bool in_entry = false;
  bool wrote_exec = false;
  for (const auto& raw_line : shipped->split('\n')) {
    auto line = QString::fromUtf8(raw_line);
    if (line.endsWith(QLatin1Char('\r'))) {
      line.chop(1);
    }
    const auto trimmed = line.trimmed();
    if (trimmed.startsWith(QLatin1Char('['))) {
      in_entry = trimmed == QLatin1String(kDesktopEntryGroup);
    } else if (trimmed.startsWith(QStringLiteral("TryExec=")) ||
               trimmed.startsWith(QString::fromLatin1(kAppImageMenuEntryMarkerKey) + QLatin1Char('='))) {
      continue;
    } else if (trimmed.startsWith(QStringLiteral("Exec="))) {
      lines.push_back(QStringLiteral("Exec=") + exec_with_program(trimmed.mid(5), program));
      if (in_entry && !wrote_exec) {
        // TryExec hides the entry once the AppImage is gone instead of leaving one that
        // does nothing.
        lines.push_back(QStringLiteral("TryExec=") + desktop_string_value(paths.appimage_file));
        lines.push_back(QString::fromLatin1(kAppImageMenuEntryMarkerKey) + QStringLiteral("=true"));
        wrote_exec = true;
      }
      continue;
    }
    lines.push_back(line);
  }
  if (!wrote_exec) {
    return fail(AppImageMenuEntryStrings::tr("This AppImage's desktop file has no Exec line"));
  }
  while (!lines.isEmpty() && lines.back().isEmpty()) {
    lines.pop_back();
  }
  return (lines.join(QLatin1Char('\n')) + QLatin1Char('\n')).toUtf8();
}

AppImageMenuEntryResult add_appimage_menu_entry(const AppImageMenuEntryPaths& paths) {
  switch (appimage_menu_entry_state(paths)) {
    case AppImageMenuEntryState::Unavailable:
      return {false, false, AppImageMenuEntryStrings::tr("Patchy is not running from an AppImage")};
    case AppImageMenuEntryState::AddedElsewhere:
      return {false, false, AppImageMenuEntryStrings::tr("Patchy is already in the applications menu")};
    case AppImageMenuEntryState::NotAdded:
    case AppImageMenuEntryState::Added:
      break;
  }
  return write_entry(paths);
}

AppImageMenuEntryResult remove_appimage_menu_entry(const AppImageMenuEntryPaths& paths) {
  AppImageMenuEntryResult result;
  if (appimage_menu_entry_state(paths) != AppImageMenuEntryState::Added) {
    result.error = AppImageMenuEntryStrings::tr("Patchy did not add the applications menu entry");
    return result;
  }
  // Exactly the files Add writes, each a checked full path; the folders stay.
  QStringList files{paths.desktop_file()};
  files += paths.icon_files();
  for (const auto& file : files) {
    if (!QFileInfo::exists(file)) {
      continue;
    }
    if (!QFile::remove(file)) {
      result.error = AppImageMenuEntryStrings::tr("Could not remove %1").arg(QDir::toNativeSeparators(file));
      return result;
    }
    result.changed = true;
  }
  result.ok = true;
  return result;
}

AppImageMenuEntryResult refresh_appimage_menu_entry(const AppImageMenuEntryPaths& paths) {
  if (appimage_menu_entry_state(paths) != AppImageMenuEntryState::Added) {
    return {true, false, {}};
  }
  return write_entry(paths);
}

void refresh_desktop_menu_caches(const AppImageMenuEntryPaths& paths) {
#ifdef Q_OS_LINUX
  if (const auto tool = QStandardPaths::findExecutable(QStringLiteral("update-desktop-database")); !tool.isEmpty()) {
    (void)QProcess::startDetached(tool, {QStringLiteral("-q"), applications_dir(paths.data_home)});
  }
  const auto hicolor = QDir(paths.data_home).filePath(QStringLiteral("icons/hicolor"));
  if (QFileInfo::exists(QDir(hicolor).filePath(QStringLiteral("icon-theme.cache")))) {
    if (const auto tool = QStandardPaths::findExecutable(QStringLiteral("gtk-update-icon-cache")); !tool.isEmpty()) {
      (void)QProcess::startDetached(tool, {QStringLiteral("-q"), QStringLiteral("-t"), QStringLiteral("-f"), hicolor});
    }
  }
#else
  (void)paths;
#endif
}

}  // namespace patchy::ui
