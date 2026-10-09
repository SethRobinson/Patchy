#include "ui/legacy_plugin_folder.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

namespace patchy::ui {

namespace {

// Keep in step with packaging/plugins/README.txt; ui_legacy_plugins_readme_matches_packaged_file
// compares the two.
constexpr const char* kReadmeText =
    "Patchy plug-ins folder\n"
    "======================\n"
    "\n"
    "Put classic Photoshop filter plug-ins (.8bf files) in this folder, or in\n"
    "subfolders of it. Both 32-bit and 64-bit plug-ins work.\n"
    "\n"
    "After adding files, choose Plugins > Rescan Plug-in Folders in Patchy (the\n"
    "folder is also scanned every time Patchy starts). Each plug-in then appears\n"
    "under Plugins > Legacy Photoshop Plug-ins, grouped by the category it\n"
    "declares, and runs on the active pixel layer inside the selection, with its\n"
    "own settings dialog and preview.\n"
    "\n"
    "Notes:\n"
    "- Only filter plug-ins (.8bf) run. File-format (.8bi) and automation (.8li)\n"
    "  plug-ins are listed as unsupported. DDS textures need no plug-in: Patchy\n"
    "  opens and saves .dds files natively.\n"
    "- Plug-ins are Windows programs. The macOS and Linux builds of Patchy cannot\n"
    "  run them.\n"
    "- Each plug-in runs in a separate helper program (patchy-8bf-host32.exe or\n"
    "  patchy-8bf-host64.exe), so a crashing plug-in cannot take Patchy down. It\n"
    "  still runs with your user permissions: only install plug-ins you trust.\n"
    "- More folders can be added under File > Preferences > Plug-ins.\n"
    "\n"
    "Details: https://github.com/SethRobinson/Patchy/blob/main/docs/plugins.md\n";

}  // namespace

QString legacy_plugins_folder_path() {
#ifdef Q_OS_WIN
  return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("plugins"));
#else
  return QString();
#endif
}

QString legacy_plugins_readme_text() { return QString::fromLatin1(kReadmeText); }

bool ensure_legacy_plugins_folder(QString* error) {
  const auto folder = legacy_plugins_folder_path();
  if (folder.isEmpty()) {
    if (error != nullptr) {
      *error = QCoreApplication::translate("LegacyPluginFolder", "Legacy Photoshop plug-ins run on Windows only.");
    }
    return false;
  }
  if (!QDir().mkpath(folder)) {
    if (error != nullptr) {
      *error = QCoreApplication::translate("LegacyPluginFolder", "The plug-ins folder could not be created: %1")
                   .arg(QDir::toNativeSeparators(folder));
    }
    return false;
  }
  const auto readme = QDir(folder).filePath(QStringLiteral("README.txt"));
  if (!QFileInfo::exists(readme)) {
    // Best effort: a missing README never blocks opening the folder.
    QSaveFile file(readme);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
      file.write(legacy_plugins_readme_text().toUtf8());
      (void)file.commit();
    }
  }
  return true;
}

}  // namespace patchy::ui
