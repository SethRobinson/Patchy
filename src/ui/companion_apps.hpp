#pragma once
// Plugins > Companions: external apps that work on the open document through the attached
// connector (docs/companions.md). Each one is a JSON manifest in the per-user companions
// folder; picking its entry tells the running companion to show itself, or starts it with
// the connector endpoint in its environment. The manifest keys are a file-format contract.
#include <QObject>
#include <QProcessEnvironment>
#include <QString>
#include <QStringList>

#include <functional>
#include <vector>

class QMenu;

namespace patchy::ui {
class MainWindow;

struct CompanionManifest {
  QString id;          // reverse-DNS, unique: "io.github.example.openfill"
  QString name;        // shown in messages
  QString menu_label;  // the menu entry; defaults to name
  QString command;     // absolute, relative to the manifest's folder, or found on PATH
  QStringList args;
  QString working_directory;  // empty: the manifest's folder
  int min_protocol = 1;       // the companion protocol range it works with (docs/companions.md#protocol)
  int max_protocol = 0;       // 0: no upper limit
  QString manifest_path;
};

// Whether this Patchy serves a protocol version inside the manifest's range.
[[nodiscard]] bool companion_protocol_supported(const CompanionManifest& manifest);

// <per-user app data>/companions. Created on demand by the menu's Open Folder entry.
[[nodiscard]] QString companions_folder_path();
// Every valid *.json manifest in the folder, sorted by menu label. Invalid files are
// skipped; `problems` (when given) receives one translated line per skipped file.
[[nodiscard]] std::vector<CompanionManifest> load_companion_manifests(QStringList* problems = nullptr);
// Reads one manifest; empty `id` with `error` set when the file is not a valid manifest.
[[nodiscard]] CompanionManifest read_companion_manifest(const QString& path, QString* error);
// The program a manifest starts, resolved as `command` describes; empty when not found.
[[nodiscard]] QString resolve_companion_command(const CompanionManifest& manifest);
// The environment a companion starts with: this process's plus PATCHY_MCP_ENDPOINT,
// PATCHY_COMPANION_ID, PATCHY_EXECUTABLE and PATCHY_MCP (the connector, when installed; inside
// an AppImage both name the .AppImage file, whose connector takes the patchy-mcp argument).
[[nodiscard]] QProcessEnvironment companion_environment(const CompanionManifest& manifest);

// Owns the Companions submenu, parented to the Plugins menu. The entries are rebuilt from
// the folder each time the submenu opens, with a Quit entry for each companion attached.
class CompanionMenu final : public QObject {
  Q_OBJECT
 public:
  using Launcher = std::function<bool(const QString& program, const QStringList& args,
                                      const QString& working_directory, const QProcessEnvironment& environment)>;
  CompanionMenu(QMenu* plugins_menu, MainWindow& window);
  [[nodiscard]] QMenu* menu() const { return menu_; }
  void rebuild();
  // Show the running companion, or start it. Returns false when it could not be started.
  bool activate(const CompanionManifest& manifest);
  // Asks the attached companion to quit (companionCommand "quit"). False when none is attached.
  bool quit(const CompanionManifest& manifest);
  // Tests replace QProcess::startDetached.
  void set_launcher_for_testing(Launcher launcher) { launcher_ = std::move(launcher); }
  // False inside a Flatpak sandbox, which cannot start host programs: the artist starts the
  // companion and it attaches (docs/companions.md#companions-menu).
  void set_can_launch_for_testing(bool can_launch) { can_launch_ = can_launch; }

 protected:
  bool eventFilter(QObject* watched, QEvent* event) override;

 private:
  MainWindow& window_;
  QMenu* menu_;
  Launcher launcher_;
  bool can_launch_;
};
}  // namespace patchy::ui
