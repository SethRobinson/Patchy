// The script browser model (docs/scripting.md): recursive bundled/user folder
// scans and the shadow-override merge consumed by both the File > Scripts menu
// (main_window_scripting.cpp) and the Script Editor tree
// (script_editor_dialog.cpp).

#include "ui/script_folders.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

#include <algorithm>
#include <functional>
#include <set>

namespace patchy::ui {

namespace {

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
  const auto files = dir.entryInfoList({QStringLiteral("*.js")}, QDir::Files | QDir::Readable,
                                       QDir::Name | QDir::IgnoreCase);
  for (const auto& file : files) {
    ScriptFolderEntry entry;
    entry.name = file.completeBaseName();
    entry.file_name = file.fileName();
    entry.relative_path = prefix + file.fileName();
    entry.path = file.absoluteFilePath();
    out.push_back(std::move(entry));
  }
}

// Applies user shadow copies onto the bundled tree and collects the relative
// paths that were consumed as overrides.
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

}  // namespace patchy::ui
