#include "ui/companion_apps.hpp"

#include "ui/ai_control_paths.hpp"
#include "ui/appimage_environment.hpp"
#include "ui/command_search.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/main_window.hpp"
#include "ui/mcp_attachment.hpp"
#include "ui/mcp_session.hpp"

#include <QAction>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMenu>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QStatusBar>
#include <QUrl>

#include <algorithm>

namespace patchy::ui {

QString companions_folder_path() {
  return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/companions");
}

CompanionManifest read_companion_manifest(const QString& path, QString* error) {
  CompanionManifest manifest;
  const auto fail = [&](const QString& message) {
    if (error != nullptr) { *error = message; }
    return CompanionManifest{};
  };
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly) || file.size() > 64 * 1024) {
    return fail(CompanionMenu::tr("%1 could not be read.").arg(QFileInfo(path).fileName()));
  }
  QJsonParseError parse_error;
  const auto document = QJsonDocument::fromJson(file.readAll(), &parse_error);
  if (parse_error.error != QJsonParseError::NoError || !document.isObject()) {
    return fail(CompanionMenu::tr("%1 is not a JSON object.").arg(QFileInfo(path).fileName()));
  }
  const auto object = document.object();
  // Permanent manifest keys (docs/companions.md): id, name, menuLabel, command, args, workingDirectory,
  // minProtocol, maxProtocol.
  static const QRegularExpression id_pattern(QStringLiteral("^[A-Za-z0-9_-]+(\\.[A-Za-z0-9_-]+)+$"));
  manifest.id = object["id"].toString();
  manifest.name = object["name"].toString().trimmed();
  manifest.menu_label = object["menuLabel"].toString(manifest.name).trimmed();
  manifest.command = object["command"].toString();
  manifest.working_directory = object["workingDirectory"].toString();
  manifest.manifest_path = QFileInfo(path).absoluteFilePath();
  for (const auto& value : object["args"].toArray()) {
    if (!value.isString()) {
      return fail(CompanionMenu::tr("%1: every args entry must be a string.").arg(QFileInfo(path).fileName()));
    }
    manifest.args.append(value.toString());
  }
  if (!id_pattern.match(manifest.id).hasMatch() || manifest.id.size() > 128 || manifest.name.isEmpty() ||
      manifest.menu_label.isEmpty() || manifest.command.isEmpty()) {
    return fail(CompanionMenu::tr("%1 needs a reverse-DNS id, a name and a command.").arg(QFileInfo(path).fileName()));
  }
  const auto protocol = [&object](const char* key, int fallback) {
    const auto value = object.value(QLatin1String(key));
    if (value.isUndefined()) { return fallback; }
    const double number = value.toDouble(-1.0);
    return value.isDouble() && number >= 1.0 && number <= 1000.0 && number == static_cast<int>(number)
               ? static_cast<int>(number)
               : -1;
  };
  manifest.min_protocol = protocol("minProtocol", 1);
  manifest.max_protocol = protocol("maxProtocol", 0);
  if (manifest.min_protocol < 1 || manifest.max_protocol < 0 ||
      (manifest.max_protocol != 0 && manifest.max_protocol < manifest.min_protocol)) {
    return fail(CompanionMenu::tr("%1: minProtocol and maxProtocol must be whole numbers from 1, the minimum first.")
                    .arg(QFileInfo(path).fileName()));
  }
  return manifest;
}

bool companion_protocol_supported(const CompanionManifest& manifest) {
  return manifest.min_protocol <= kCompanionProtocolMax &&
         (manifest.max_protocol == 0 || manifest.max_protocol >= kCompanionProtocolMin);
}

std::vector<CompanionManifest> load_companion_manifests(QStringList* problems) {
  std::vector<CompanionManifest> manifests;
  const QDir folder(companions_folder_path());
  for (const auto& name : folder.entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name)) {
    QString error;
    auto manifest = read_companion_manifest(folder.absoluteFilePath(name), &error);
    const bool duplicate = std::any_of(manifests.begin(), manifests.end(),
                                       [&](const CompanionManifest& other) { return other.id == manifest.id; });
    if (manifest.id.isEmpty() || duplicate) {
      if (problems != nullptr) {
        problems->append(duplicate ? CompanionMenu::tr("%1 repeats the id %2.").arg(name, manifest.id) : error);
      }
      continue;
    }
    manifests.push_back(std::move(manifest));
  }
  std::sort(manifests.begin(), manifests.end(), [](const CompanionManifest& a, const CompanionManifest& b) {
    return QString::localeAwareCompare(a.menu_label, b.menu_label) < 0;
  });
  return manifests;
}

QString companion_bundle_executable(const QString& bundle_path) {
  const QDir bundle(bundle_path);
  QString name;
  QFile plist(bundle.absoluteFilePath(QStringLiteral("Contents/Info.plist")));
  if (plist.size() <= 1024 * 1024 && plist.open(QIODevice::ReadOnly)) {
    // XML property lists only; a binary one falls back to the bundle's own name.
    static const QRegularExpression executable_key(
        QStringLiteral("<key>\\s*CFBundleExecutable\\s*</key>\\s*<string>([^<]+)</string>"));
    const auto match = executable_key.match(QString::fromUtf8(plist.readAll()));
    if (match.hasMatch()) { name = match.captured(1).trimmed(); }
  }
  if (name.isEmpty() || name.contains(QLatin1Char('/'))) { name = QFileInfo(bundle_path).completeBaseName(); }
  const QFileInfo executable(bundle.absoluteFilePath(QStringLiteral("Contents/MacOS/") + name));
  return executable.isFile() ? executable.absoluteFilePath() : QString();
}

namespace {
// A file runs as is; an app bundle (a folder named *.app) runs its Contents/MacOS executable,
// so the companion still gets its arguments and environment (`open -a` would drop both).
QString runnable_path(const QFileInfo& info) {
  if (info.isFile()) { return info.absoluteFilePath(); }
  if (info.isDir() && info.suffix().compare(QStringLiteral("app"), Qt::CaseInsensitive) == 0) {
    return companion_bundle_executable(info.absoluteFilePath());
  }
  return {};
}
}  // namespace

QString resolve_companion_command(const CompanionManifest& manifest) {
  const QFileInfo command(manifest.command);
  if (command.isAbsolute()) { return runnable_path(command); }
  if (manifest.command.contains(QLatin1Char('/')) || manifest.command.contains(QLatin1Char('\\'))) {
    return runnable_path(QFileInfo(QFileInfo(manifest.manifest_path).absoluteDir(), manifest.command));
  }
  return QStandardPaths::findExecutable(manifest.command);
}

QString companion_working_directory(const CompanionManifest& manifest) {
  const QDir manifest_folder = QFileInfo(manifest.manifest_path).absoluteDir();
  if (manifest.working_directory.isEmpty()) { return manifest_folder.absolutePath(); }
  return QDir::cleanPath(manifest_folder.absoluteFilePath(manifest.working_directory));
}

QProcessEnvironment companion_environment(const CompanionManifest& manifest) {
  auto environment = QProcessEnvironment::systemEnvironment();
  const auto appimage = appimage_file_path();
  if (!appimage.isEmpty()) {
    // What the AppImage runtime and AppRun set for Patchy itself, which would mislead a
    // companion that is a Qt program or an AppImage of its own (packaging/linux/appimage/AppRun).
    for (const auto* name : {"APPDIR", "APPIMAGE", "ARGV0", "OWD"}) { environment.remove(QLatin1String(name)); }
    if (environment.contains(QLatin1String(kAppRunSetPlatformThemeVariable))) {
      environment.remove(QStringLiteral("QT_QPA_PLATFORMTHEME"));
      environment.remove(QLatin1String(kAppRunSetPlatformThemeVariable));
    }
  }
  environment.insert(QStringLiteral("PATCHY_MCP_ENDPOINT"), mcp_attachment_endpoint());
  environment.insert(QStringLiteral("PATCHY_COMPANION_ID"), manifest.id);
  environment.insert(QStringLiteral("PATCHY_EXECUTABLE"), user_visible_executable_path());
  // Inside an AppImage the .AppImage file is the connector, run with the patchy-mcp argument
  // (its mount, where a patchy-mcp beside the executable would live, is gone once Patchy exits).
  // PATCHY_MCP_ARGS is the JSON array of arguments that follow PATCHY_MCP to attach.
  const auto paths = resolve_ai_control_paths();
  const auto connector = paths.appimage_path.isEmpty() ? paths.connector_path : paths.appimage_path;
  if (!connector.isEmpty()) {
    environment.insert(QStringLiteral("PATCHY_MCP"), connector);
    QJsonArray args;
    if (!paths.appimage_path.isEmpty()) { args.append(QLatin1String(kAppImageConnectorArgument)); }
    args.append(QStringLiteral("--attach"));
    environment.insert(QStringLiteral("PATCHY_MCP_ARGS"),
                       QString::fromUtf8(QJsonDocument(args).toJson(QJsonDocument::Compact)));
  }
  return environment;
}

CompanionMenu::CompanionMenu(QMenu* plugins_menu, MainWindow& window)
    : QObject(plugins_menu), window_(window), menu_(plugins_menu->addMenu(tr("&Companions"))),
      can_launch_(!resolve_ai_control_paths().flatpak) {
  menu_->setObjectName(QStringLiteral("companionsMenu"));
  menu_->menuAction()->setMenuRole(QAction::NoRole);
  launcher_ = [](const QString& program, const QStringList& args, const QString& working_directory,
                 const QProcessEnvironment& environment) {
#if QT_CONFIG(process)
    QProcess process;
    process.setProgram(program);
    process.setArguments(args);
    process.setWorkingDirectory(working_directory);
    process.setProcessEnvironment(environment);
    return process.startDetached();
#else
    // wasm Qt has no QProcess; the web build has no Companions menu either.
    (void)program;
    (void)args;
    (void)working_directory;
    (void)environment;
    return false;
#endif
  };
  connect(menu_, &QMenu::aboutToShow, this, [this] { rebuild(); });
  menu_->installEventFilter(this);
  rebuild();
}

bool CompanionMenu::eventFilter(QObject* watched, QEvent* event) {
  if (watched == menu_ && event->type() == QEvent::LanguageChange) {
    menu_->setTitle(tr("&Companions"));
    rebuild();
  }
  return QObject::eventFilter(watched, event);
}

void CompanionMenu::set_builtin_action(QAction* action) {
  builtin_action_ = action;
  rebuild();
}

void CompanionMenu::rebuild() {
  // clear() deletes only the actions the menu owns; the built-in one belongs to the window.
  menu_->clear();
  if (builtin_action_ != nullptr) {
    menu_->addAction(builtin_action_);
    menu_->addSeparator();
  }
  QStringList problems;
  const auto manifests = load_companion_manifests(&problems);
  for (const auto& manifest : manifests) {
    if (McpAttachment::companion_attached(manifest.id)) { launching_.erase(manifest.id); }
    const auto label = QString(manifest.menu_label).replace(QLatin1Char('&'), QStringLiteral("&&"));
    const bool supported = companion_protocol_supported(manifest);
    const bool reachable = can_launch_ || McpAttachment::companion_attached(manifest.id);
    auto* action = menu_->addAction(!supported ? (manifest.min_protocol > kCompanionProtocolMax
                                                      ? tr("%1 (needs a newer Patchy)").arg(label)
                                                      : tr("%1 (needs an update)").arg(label))
                                    : reachable ? label
                                                : tr("%1 (start it outside Patchy)").arg(label));
    action->setEnabled(supported && reachable);
    action->setObjectName(QStringLiteral("companionAction_") + manifest.id);
    action->setMenuRole(QAction::NoRole);
    action->setToolTip(manifest.name);
    action->setProperty(kCommandSearchDataEntryProperty, true);  // one per manifest file
    action->setProperty(kWorksWithoutDocumentProperty, true);  // a companion can make its own
    connect(action, &QAction::triggered, this, [this, manifest] { (void)activate(manifest); });
  }
  bool quit_section = false;
  for (const auto& manifest : manifests) {
    if (!McpAttachment::companion_attached(manifest.id)) {
      continue;
    }
    if (!quit_section) {
      menu_->addSeparator();
      quit_section = true;
    }
    auto* quit_action = menu_->addAction(tr("Quit %1").arg(QString(manifest.name).replace(QLatin1Char('&'), QStringLiteral("&&"))));
    quit_action->setObjectName(QStringLiteral("companionQuitAction_") + manifest.id);
    quit_action->setMenuRole(QAction::NoRole);
    quit_action->setProperty(kCommandSearchDataEntryProperty, true);  // shown only while it runs
    quit_action->setProperty(kWorksWithoutDocumentProperty, true);
    connect(quit_action, &QAction::triggered, this, [this, manifest] { (void)quit(manifest); });
  }
  if (manifests.empty() && problems.isEmpty()) {
    auto* none = menu_->addAction(tr("No companion apps installed"));
    none->setObjectName(QStringLiteral("companionsNoneAction"));
    none->setEnabled(false);
    none->setMenuRole(QAction::NoRole);
    none->setProperty(kCommandSearchDataEntryProperty, true);  // a folder state, not a command
  }
  menu_->addSeparator();
  if (!problems.isEmpty()) {
    // A manifest that is skipped says why, instead of its entry just not appearing.
    auto* skipped = menu_->addAction(tr("Manifests That Could Not Be Read..."));
    skipped->setObjectName(QStringLiteral("companionsProblemsAction"));
    skipped->setMenuRole(QAction::NoRole);
    skipped->setProperty(kCommandSearchDataEntryProperty, true);  // shown only while a file is broken
    skipped->setProperty(kWorksWithoutDocumentProperty, true);
    connect(skipped, &QAction::triggered, this, [this, problems] {
      show_information_message(&window_, tr("Companion Manifests"),
                               tr("These files in the companions folder were skipped:") + QStringLiteral("\n\n") +
                                   problems.join(QLatin1Char('\n')),
                               QStringLiteral("companionProblemsMessage"));
    });
  }
  auto* open_folder = menu_->addAction(tr("Open Companions Folder"));
  open_folder->setObjectName(QStringLiteral("companionsOpenFolderAction"));
  open_folder->setMenuRole(QAction::NoRole);
  open_folder->setProperty(kWorksWithoutDocumentProperty, true);
  connect(open_folder, &QAction::triggered, this, [] {
    const auto folder = companions_folder_path();
    (void)QDir().mkpath(folder);
    (void)QDesktopServices::openUrl(QUrl::fromLocalFile(folder));
  });
}

bool CompanionMenu::activate(const CompanionManifest& manifest) {
  if (!companion_protocol_supported(manifest)) {
    return false;
  }
  if (McpAttachment::send_companion_command(manifest.id, QStringLiteral("show"))) {
    launching_.erase(manifest.id);
    window_.statusBar()->showMessage(tr("Asked %1 to show its panel").arg(manifest.name), 4000);
    return true;
  }
  if (!can_launch_) {
    return false;
  }
  // A companion attaches only once it has started, so a second pick before that would start a
  // second copy; until it attaches, or the grace period ends, the pick just says it is starting.
  if (const auto pending = launching_.find(manifest.id);
      pending != launching_.end() && !pending->second.hasExpired()) {
    window_.statusBar()->showMessage(tr("%1 is starting...").arg(manifest.name), 4000);
    return true;
  }
  const auto program = resolve_companion_command(manifest);
  if (program.isEmpty() ||
      !launcher_(program, manifest.args, companion_working_directory(manifest), companion_environment(manifest))) {
    window_.statusBar()->showMessage(tr("Could not start %1 (%2)").arg(manifest.name, manifest.command), 8000);
    return false;
  }
  launching_.insert_or_assign(manifest.id, QDeadlineTimer(launch_grace_ms_));
  window_.statusBar()->showMessage(tr("Starting %1...").arg(manifest.name), 4000);
  return true;
}

bool CompanionMenu::quit(const CompanionManifest& manifest) {
  if (!McpAttachment::send_companion_command(manifest.id, QStringLiteral("quit"))) {
    return false;
  }
  launching_.erase(manifest.id);
  window_.statusBar()->showMessage(tr("Asked %1 to quit").arg(manifest.name), 4000);
  return true;
}

}  // namespace patchy::ui
