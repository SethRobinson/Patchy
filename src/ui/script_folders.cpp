// The script browser model (docs/scripting.md): recursive bundled/user folder
// scans with header metadata (@name / @window) and sidecar icon PNGs, the
// shadow-override merge, and the icon write helpers, consumed by both the
// File > Scripts menu (main_window_scripting.cpp) and the Script Manager tree
// (script_editor_dialog.cpp).

#include "ui/script_folders.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPixmap>
#include <QPolygonF>
#include <QTextStream>

#include <algorithm>
#include <cmath>
#include <functional>
#include <set>

namespace patchy::ui {

namespace {

// The sidecar icon convention: "Games/breakout.js" -> "Games/breakout.png".
QString icon_relative_path(const QString& script_relative_path) {
  const int dot = script_relative_path.lastIndexOf(QLatin1Char('.'));
  const auto base = dot > 0 ? script_relative_path.left(dot) : script_relative_path;
  return base + QStringLiteral(".png");
}

void scan_into(const QDir& dir, const QString& prefix, std::vector<ScriptFolderEntry>& out) {
  const auto folders =
      dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Readable, QDir::Name);
  for (const auto& folder : folders) {
    ScriptFolderEntry entry;
    entry.name = folder.fileName();
    entry.relative_path = prefix + folder.fileName();
    entry.is_folder = true;
    scan_into(QDir(folder.absoluteFilePath()), entry.relative_path + QLatin1Char('/'),
              entry.children);
    if (!entry.children.empty()) {
      out.push_back(std::move(entry));
    }
  }
  const auto files_begin = out.size();
  const auto files = dir.entryInfoList({QStringLiteral("*.js")}, QDir::Files | QDir::Readable,
                                       QDir::Name | QDir::IgnoreCase);
  for (const auto& file : files) {
    ScriptFolderEntry entry;
    entry.name = file.completeBaseName();
    entry.file_name = file.fileName();
    entry.relative_path = prefix + file.fileName();
    entry.path = file.absoluteFilePath();
    const auto meta = read_script_metadata(entry.path);
    entry.display_name = meta.name.isEmpty() ? entry.name : meta.name;
    entry.description = meta.description;
    entry.author = meta.author;
    entry.cli_example = meta.cli_example;
    entry.opens_window = meta.opens_window;
    const auto icon = dir.absoluteFilePath(file.completeBaseName() + QStringLiteral(".png"));
    if (QFileInfo::exists(icon)) {
      entry.icon_path = icon;
    }
    out.push_back(std::move(entry));
  }
  std::sort(out.begin() + static_cast<std::ptrdiff_t>(files_begin), out.end(),
            [](const ScriptFolderEntry& a, const ScriptFolderEntry& b) {
              const int by_display = QString::compare(a.display_name, b.display_name,
                                                      Qt::CaseInsensitive);
              return by_display != 0 ? by_display < 0
                                     : QString::compare(a.name, b.name, Qt::CaseInsensitive) < 0;
            });
}

// Applies user shadow copies (and user icon overrides) onto the bundled tree
// and collects the relative paths consumed as script overrides.
void apply_overrides(std::vector<ScriptFolderEntry>& bundled, const QString& user_root,
                     std::set<QString>& overridden) {
  for (auto& entry : bundled) {
    if (entry.is_folder) {
      apply_overrides(entry.children, user_root, overridden);
      continue;
    }
    const QString candidate = user_root + QLatin1Char('/') + entry.relative_path;
    if (QFileInfo::exists(candidate)) {
      entry.bundled_path = entry.path;
      entry.path = QFileInfo(candidate).absoluteFilePath();
      entry.is_override = true;
      overridden.insert(entry.relative_path);
      // Metadata follows the file that runs.
      const auto meta = read_script_metadata(entry.path);
      entry.display_name = meta.name.isEmpty() ? entry.name : meta.name;
      entry.description = meta.description;
      entry.author = meta.author;
      entry.cli_example = meta.cli_example;
      entry.opens_window = meta.opens_window;
    }
    // A user icon shadows the bundled one on its own ("Set Icon..." writes
    // here so shipped files stay pristine).
    const QString icon_candidate =
        user_root + QLatin1Char('/') + icon_relative_path(entry.relative_path);
    if (QFileInfo::exists(icon_candidate)) {
      entry.icon_path = QFileInfo(icon_candidate).absoluteFilePath();
    }
  }
}

// Drops user entries that shadow a bundled script (they render inside the
// Bundled tree instead) and folders left empty by that.
void prune_overrides(std::vector<ScriptFolderEntry>& entries, const std::set<QString>& overridden) {
  for (auto& entry : entries) {
    if (entry.is_folder) {
      prune_overrides(entry.children, overridden);
    }
  }
  entries.erase(std::remove_if(entries.begin(), entries.end(),
                               [&overridden](const ScriptFolderEntry& entry) {
                                 return entry.is_folder
                                            ? entry.children.empty()
                                            : overridden.count(entry.relative_path) > 0;
                               }),
                entries.end());
}

}  // namespace

ScriptMetadata read_script_metadata(const QString& path) {
  ScriptMetadata meta;
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return meta;
  }
  QTextStream stream(&file);
  for (int line_number = 0; line_number < 30 && !stream.atEnd(); ++line_number) {
    const auto line = stream.readLine(512).trimmed();
    if (line.isEmpty()) {
      continue;
    }
    if (!line.startsWith(QLatin1String("//"))) {
      break;  // directives live in the top comment block only
    }
    const auto body = QStringView(line).mid(2).trimmed();
    if (body.startsWith(QLatin1String("@name "))) {
      meta.name = body.mid(6).trimmed().toString();
    } else if (body.startsWith(QLatin1String("@description "))) {
      // Repeated @description lines continue the text.
      const auto text = body.mid(13).trimmed().toString();
      meta.description =
          meta.description.isEmpty() ? text : meta.description + QLatin1Char(' ') + text;
    } else if (body.startsWith(QLatin1String("@author "))) {
      meta.author = body.mid(8).trimmed().toString();
    } else if (body.startsWith(QLatin1String("@cli "))) {
      // Repeated @cli lines continue the token list.
      const auto text = body.mid(5).trimmed().toString();
      meta.cli_example =
          meta.cli_example.isEmpty() ? text : meta.cli_example + QLatin1Char(' ') + text;
    } else if (body == QLatin1String("@window")) {
      meta.opens_window = true;
    }
  }
  return meta;
}

QString script_cli_example_command(const QString& exe_path, const QString& script_path,
                                   const ScriptMetadata& meta) {
  // Always quoted (paths under Program Files have spaces); cmd.exe accepts a
  // quoted program path directly, and the dialog notes cover PowerShell's
  // leading "&".
  const auto quoted = [](const QString& path) {
    return QLatin1Char('"') + QDir::toNativeSeparators(path) + QLatin1Char('"');
  };
  auto command = quoted(exe_path) + QStringLiteral(" --run-script ") + quoted(script_path);
  if (!meta.cli_example.isEmpty()) {
    command += QLatin1Char(' ') + meta.cli_example;
  } else if (!meta.opens_window) {
    // Active-document scripts need a document; a positional file opens first.
    command += QStringLiteral(" example.png");
  }
  return command;
}

std::vector<ScriptFolderEntry> scan_script_folder(const QString& root) {
  std::vector<ScriptFolderEntry> out;
  if (root.isEmpty()) {
    return out;
  }
  const QDir dir(root);
  if (!dir.exists()) {
    return out;
  }
  scan_into(dir, QString(), out);
  return out;
}

ScriptScan scan_scripts(const QString& bundled_root, const QString& user_root) {
  ScriptScan scan;
  scan.bundled = scan_script_folder(bundled_root);
  scan.user = scan_script_folder(user_root);
  if (!user_root.isEmpty() && !scan.bundled.empty()) {
    std::set<QString> overridden;
    apply_overrides(scan.bundled, QDir(user_root).absolutePath(), overridden);
    if (!overridden.empty()) {
      prune_overrides(scan.user, overridden);
    }
  }
  return scan;
}

QString relative_path_under(const QString& root, const QString& path) {
  if (root.isEmpty() || path.isEmpty()) {
    return {};
  }
  const auto relative = QDir(root).relativeFilePath(path);
  if (relative.isEmpty() || relative == QLatin1String(".") || QDir::isAbsolutePath(relative) ||
      relative.startsWith(QLatin1String(".."))) {
    return {};
  }
  return relative;
}

QString script_folder_display_name(const QString& folder_name) {
  // The four well-known bundled folders; anything else (user-made folders)
  // shows its raw name.
  if (folder_name == QLatin1String("Games")) {
    return QCoreApplication::translate("ScriptFolders", "Games");
  }
  if (folder_name == QLatin1String("Demos")) {
    return QCoreApplication::translate("ScriptFolders", "Demos");
  }
  if (folder_name == QLatin1String("Effects")) {
    return QCoreApplication::translate("ScriptFolders", "Effects");
  }
  if (folder_name == QLatin1String("Utilities")) {
    return QCoreApplication::translate("ScriptFolders", "Utilities");
  }
  return folder_name;
}

QString script_icon_write_target(const QString& user_root, const QString& relative_path) {
  if (user_root.isEmpty() || relative_path.isEmpty()) {
    return {};
  }
  return QDir(user_root).absoluteFilePath(icon_relative_path(relative_path));
}

bool write_script_icon(const QImage& image, const QString& target) {
  if (image.isNull() || target.isEmpty()) {
    return false;
  }
  // 128x128 so the hover card can show it large; the tree scales it to 32.
  const int side = std::min(image.width(), image.height());
  const QImage square = image.copy((image.width() - side) / 2, (image.height() - side) / 2,
                                   side, side);
  const QImage scaled =
      square.scaled(128, 128, Qt::KeepAspectRatio, Qt::SmoothTransformation);
  if (!QDir().mkpath(QFileInfo(target).absolutePath())) {
    return false;
  }
  return scaled.save(target, "PNG");
}

QIcon script_entry_icon(const ScriptFolderEntry& entry) {
  if (!entry.icon_path.isEmpty()) {
    const QPixmap pixmap(entry.icon_path);
    if (!pixmap.isNull()) {
      return QIcon(pixmap);
    }
  }
  return script_generic_icon();
}

QIcon script_generic_icon() {
  // A code page with a folded corner and a JS tag, painted in code like the
  // rest of the app's icons (action_icons.cpp style).
  static const QIcon icon = [] {
    QPixmap pixmap(64, 64);
    pixmap.fill(Qt::transparent);
    {
      QPainter painter(&pixmap);
      painter.setRenderHint(QPainter::Antialiasing);
      constexpr double left = 14.0;
      constexpr double top = 5.0;
      constexpr double right = 50.0;
      constexpr double bottom = 57.0;
      constexpr double fold = 12.0;
      QPainterPath page;
      page.moveTo(left, top);
      page.lineTo(right - fold, top);
      page.lineTo(right, top + fold);
      page.lineTo(right, bottom);
      page.lineTo(left, bottom);
      page.closeSubpath();
      painter.setPen(QPen(QColor(0x1c, 0x1f, 0x24), 2.0));
      painter.setBrush(QColor(0xdd, 0xe2, 0xe8));
      painter.drawPath(page);
      QPainterPath corner;
      corner.moveTo(right - fold, top);
      corner.lineTo(right - fold, top + fold);
      corner.lineTo(right, top + fold);
      corner.closeSubpath();
      painter.setBrush(QColor(0xa8, 0xb0, 0xba));
      painter.drawPath(corner);
      painter.setPen(QPen(QColor(0x6b, 0x74, 0x80), 3.0, Qt::SolidLine, Qt::RoundCap));
      painter.drawLine(QPointF(20, 24), QPointF(40, 24));
      painter.drawLine(QPointF(20, 31), QPointF(34, 31));
      painter.drawLine(QPointF(24, 38), QPointF(42, 38));
      painter.setPen(Qt::NoPen);
      painter.setBrush(QColor(0xe8, 0xc8, 0x3c));
      painter.drawRoundedRect(QRectF(28, 42, 26, 16), 3, 3);
      painter.setPen(QColor(0x26, 0x22, 0x10));
      QFont font = painter.font();
      font.setPixelSize(12);
      font.setBold(true);
      painter.setFont(font);
      painter.drawText(QRectF(28, 42, 26, 16), Qt::AlignCenter, QStringLiteral("JS"));
    }
    return QIcon(pixmap);
  }();
  return icon;
}

QIcon script_folder_icon() {
  static const QIcon icon = [] {
    QPixmap pixmap(64, 64);
    pixmap.fill(Qt::transparent);
    {
      QPainter painter(&pixmap);
      painter.setRenderHint(QPainter::Antialiasing);
      painter.setPen(QPen(QColor(0x2a, 0x2f, 0x36), 2.0));
      QPainterPath back;
      back.addRoundedRect(QRectF(6, 14, 52, 40), 4, 4);
      back.addRoundedRect(QRectF(8, 8, 22, 12), 3, 3);
      painter.setBrush(QColor(0x8f, 0xa3, 0xbd));
      painter.drawPath(back.simplified());
      painter.setBrush(QColor(0xa9, 0xbc, 0xd4));
      painter.drawRoundedRect(QRectF(6, 22, 52, 32), 4, 4);
    }
    return QIcon(pixmap);
  }();
  return icon;
}

QIcon script_stop_icon() {
  static const QIcon icon = [] {
    QPixmap pixmap(32, 32);
    pixmap.fill(Qt::transparent);
    {
      QPainter painter(&pixmap);
      painter.setRenderHint(QPainter::Antialiasing);
      QPolygonF octagon;
      constexpr double kPi = 3.14159265358979323846;
      const double center = 16.0;
      const double radius = 14.0;
      for (int i = 0; i < 8; ++i) {
        const double angle = (static_cast<double>(i) * 45.0 + 22.5) * kPi / 180.0;
        octagon << QPointF(center + radius * std::cos(angle),
                           center + radius * std::sin(angle));
      }
      painter.setPen(QPen(QColor(0xff, 0xff, 0xff, 0xb0), 1.5));
      painter.setBrush(QColor(0xc8, 0x32, 0x28));
      painter.drawPolygon(octagon);
      painter.setPen(Qt::NoPen);
      painter.setBrush(Qt::white);
      painter.drawRect(QRectF(10.5, 10.5, 11.0, 11.0));
    }
    return QIcon(pixmap);
  }();
  return icon;
}

QIcon script_cli_icon() {
  // A dark terminal window with a "C:\_" prompt, matching the code-drawn
  // icon family above. The glyphs are drawn big and bright so they still read
  // at the 18px toolbar-button size.
  static const QIcon icon = [] {
    QPixmap pixmap(32, 32);
    pixmap.fill(Qt::transparent);
    {
      QPainter painter(&pixmap);
      painter.setRenderHint(QPainter::Antialiasing);
      const QRectF frame(1.5, 3.5, 29.0, 25.0);
      painter.setPen(QPen(QColor(0x7a, 0x82, 0x8c), 1.6));
      painter.setBrush(QColor(0x0e, 0x11, 0x16));
      painter.drawRoundedRect(frame, 3, 3);
      // Prompt text.
      QFont font(QStringLiteral("Consolas"));
      font.setPixelSize(14);
      font.setBold(true);
      painter.setFont(font);
      painter.setPen(QColor(0xf2, 0xf5, 0xf8));
      painter.drawText(QRectF(frame.left() + 3.5, frame.top() + 2.0, frame.width() - 5.0, 15.0),
                       Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("C:\\"));
      // Blinking-cursor underscore in the accent blue.
      painter.setPen(Qt::NoPen);
      painter.setBrush(QColor(0x6f, 0xb1, 0xe8));
      painter.drawRect(QRectF(frame.left() + 4.5, frame.bottom() - 7.0, 9.0, 3.0));
    }
    return QIcon(pixmap);
  }();
  return icon;
}

}  // namespace patchy::ui
