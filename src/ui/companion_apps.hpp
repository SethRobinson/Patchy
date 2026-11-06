#pragma once
// Plugins > Companions: external apps that work on the open document through the attached
// connector (docs/companions.md). Each one is a JSON manifest in the per-user companions
// folder; picking its entry tells the running companion to show itself, or starts it with
// the connector endpoint in its environment. The manifest keys are a file-format contract.
#include <QDeadlineTimer>
#include <QObject>
#include <QPointer>
#include <QProcessEnvironment>
#include <QString>
#include <QStringList>

#include <functional>
#include <map>
#include <vector>

class QAction;
class QMenu;

namespace patchy::ui {
class MainWindow;

struct CompanionManifest {
  QString id;          // reverse-DNS, unique: "io.github.example.openfill"
  QString name;        // shown in messages
  QString menu_label;  // the menu entry; defaults to name
  QString command;     // absolute, relative to the manifest's folder, or found on PATH; a file or a *.app bundle
  QStringList args;
  QString working_directory;  // empty: the manifest's folder; relative: below the manifest's folder
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
// The program a manifest starts, resolved as `command` describes; empty when not found. An
// app bundle (a folder named *.app) resolves to Contents/MacOS/<CFBundleExecutable>.
[[nodiscard]] QString resolve_companion_command(const CompanionManifest& manifest);
// The executable inside an app bundle: Info.plist's CFBundleExecutable (XML plists), else the
// bundle's name, under Contents/MacOS. Empty when that file does not exist.
[[nodiscard]] QString companion_bundle_executable(const QString& bundle_path);
// Where the companion starts: `workingDirectory` resolved against the manifest's folder, or
// that folder itself.
[[nodiscard]] QString companion_working_directory(const CompanionManifest& manifest);
// The environment a companion starts with: this process's plus PATCHY_MCP_ENDPOINT,
// PATCHY_COMPANION_ID, PATCHY_EXECUTABLE, PATCHY_MCP (the connector, when installed; inside
// an AppImage both name the .AppImage file, whose connector takes the patchy-mcp argument) and
// PATCHY_MCP_ARGS (a JSON array of the arguments that follow PATCHY_MCP to attach). Inside an
// AppImage, what its runtime and AppRun set for Patchy (APPDIR, APPIMAGE, ARGV0, OWD and the
// portal platform theme AppRun chose) is left out.
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
  // A command built into Patchy that heads the submenu above the manifest entries (OpenAI
  // Images via API Key, docs/openai-images.md). The window owns it, so a rebuild keeps it.
  void set_builtin_action(QAction* action);
  // Show the running companion, or start it. Returns false when it could not be started.
  bool activate(const CompanionManifest& manifest);
  // Asks the attached companion to quit (companionCommand "quit"). False when none is attached.
  bool quit(const CompanionManifest& manifest);
  // Tests replace QProcess::startDetached.
  void set_launcher_for_testing(Launcher launcher) { launcher_ = std::move(launcher); }
  // False inside a Flatpak sandbox, which cannot start host programs: the artist starts the
  // companion and it attaches (docs/companions.md#companions-menu).
  void set_can_launch_for_testing(bool can_launch) { can_launch_ = can_launch; }
  void set_launch_grace_for_testing(int milliseconds) { launch_grace_ms_ = milliseconds; }

 protected:
  bool eventFilter(QObject* watched, QEvent* event) override;

 private:
  MainWindow& window_;
  QMenu* menu_;
  QPointer<QAction> builtin_action_;
  Launcher launcher_;
  bool can_launch_;
  // Companions started but not attached yet, by id: a second pick inside the grace period does
  // not start another copy.
  std::map<QString, QDeadlineTimer> launching_;
  int launch_grace_ms_ = 20000;
};
}  // namespace patchy::ui
