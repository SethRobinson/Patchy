#pragma once

// Patchy's list of the user's GIMP 3 plug-ins (docs/gimp-plugins.md, "Query and the
// menu"): finding the install and plug-in folders, starting the helper to query new or
// changed plug-in files, and the query cache (keyed by path, size and file time). No
// widgets: the scan runs on a worker thread.

#include "plugins/gimp/gimp_host_link.hpp"
#include "plugins/gimp/gimp_install.hpp"

#include <QByteArray>
#include <QString>
#include <QStringList>

#include <optional>
#include <vector>

namespace patchy::ui {

struct GimpPluginParam {
  QString name;
  QString nick;
  QString blurb;
  QString kind;       // "int", "double", "boolean", "choice", "color", ...
  QString value_type;  // GIMP's type name ("gint", "GimpRunMode")
};

struct GimpPluginProcedure {
  QString identifier;   // "gimp.<procedure>", persisted (hotkeys, scripts)
  QString procedure;    // GIMP's procedure name ("plug-in-blinds")
  QString label;        // menu label without mnemonics ("Blinds...")
  QStringList menu;     // submenu path under Plugins > GIMP 3 Plug-ins ("Distorts")
  QString blurb;
  QString help;
  QString authors;
  QString use;          // "layer", "unsupported" or "hidden"
  QString reason;       // a reason code (gimp_procedure.hpp) when not "layer"
  QString image_types;
  QString plugin_file;  // the plug-in file that registered it
  std::vector<GimpPluginParam> params;
  QByteArray install;   // GIMP wire bytes of its registration, for the helper
};

struct GimpPluginFileReport {
  QString path;
  bool ok{false};
  QString error;  // English, from the helper
  int procedures{0};
  bool from_cache{false};
};

struct GimpPluginScanResult {
  std::optional<gimp::GimpInstall> install;
  QStringList folders;  // every folder scanned
  std::vector<GimpPluginProcedure> procedures;
  std::vector<GimpPluginFileReport> files;
  QString problem;      // why nothing could be scanned (no GIMP 3, no helper), translated
  bool helper_missing{false};
};

struct GimpPluginScanOptions {
  bool enabled{true};
  QString install_folder;   // empty: detect
  QStringList extra_folders;
  // Development and tests: scan only extra_folders, never the install's own plug-ins
  // (PATCHY_GIMP_SCAN_ONLY_ADDED=1).
  bool only_extra_folders{false};
  QString language;         // the plug-ins' own text follows Patchy's language
  QString cache_file;       // empty: <AppDataLocation>/gimp-plugins/query-cache.json
  bool force_requery{false};
  int parallel_helpers{4};
};

[[nodiscard]] GimpPluginScanResult scan_gimp_plugins(const GimpPluginScanOptions& options);

// The helper next to the running application.
[[nodiscard]] QString gimp_plugin_helper_path();
// Patchy's own GIMP settings folder, handed to plug-ins as GIMP3_DIRECTORY so they never
// write into the user's GIMP settings.
[[nodiscard]] QString gimp_plugin_profile_dir();
// How to start one plug-in file of an install.
[[nodiscard]] std::optional<gimp::link::LaunchSpec> gimp_plugin_launch_spec(const gimp::GimpInstall& install,
                                                                            const QString& plugin_file,
                                                                            const QString& language, QString* error);

// The install the options select (the chosen folder, else the first detected).
[[nodiscard]] std::optional<gimp::GimpInstall> select_gimp_install(const GimpPluginScanOptions& options);

}  // namespace patchy::ui
