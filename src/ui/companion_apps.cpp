#include "ui/companion_apps.hpp"

#include "ui/ai_control_paths.hpp"
#include "ui/command_search.hpp"
#include "ui/main_window.hpp"
#include "ui/mcp_attachment.hpp"

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
  // Permanent manifest keys (docs/companions.md): id, name, menuLabel, command, args, workingDirectory.
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
  return manifest;
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

QString resolve_companion_command(const CompanionManifest& manifest) {
  const QFileInfo command(manifest.command);
  if (command.isAbsolute()) { return command.isFile() ? command.absoluteFilePath() : QString(); }
  if (manifest.command.contains(QLatin1Char('/')) || manifest.command.contains(QLatin1Char('\\'))) {
    const QFileInfo relative(QFileInfo(manifest.manifest_path).absoluteDir(), manifest.command);
    return relative.isFile() ? relative.absoluteFilePath() : QString();
  }
  return QStandardPaths::findExecutable(manifest.command);
}

QProcessEnvironment companion_environment(const CompanionManifest& manifest) {
  auto environment = QProcessEnvironment::systemEnvironment();
  environment.insert(QStringLiteral("PATCHY_MCP_ENDPOINT"), mcp_attachment_endpoint());
  environment.insert(QStringLiteral("PATCHY_COMPANION_ID"), manifest.id);
  environment.insert(QStringLiteral("PATCHY_EXECUTABLE"), QCoreApplication::applicationFilePath());
  const auto connector = resolve_ai_control_paths().connector_path;
  if (!connector.isEmpty()) { environment.insert(QStringLiteral("PATCHY_MCP"), connector); }
  return environment;
}

CompanionMenu::CompanionMenu(QMenu* plugins_menu, MainWindow& window)
    : QObject(plugins_menu), window_(window), menu_(plugins_menu->addMenu(tr("&Companions"))) {
  menu_->setObjectName(QStringLiteral("companionsMenu"));
  menu_->menuAction()->setMenuRole(QAction::NoRole);
  launcher_ = [](const QString& program, const QStringList& args, const QString& working_directory,
                 const QProcessEnvironment& environment) {
    QProcess process;
    process.setProgram(program);
    process.setArguments(args);
    process.setWorkingDirectory(working_directory);
    process.setProcessEnvironment(environment);
    return process.startDetached();
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

void CompanionMenu::rebuild() {
  menu_->clear();
  const auto manifests = load_companion_manifests();
  for (const auto& manifest : manifests) {
    auto* action = menu_->addAction(QString(manifest.menu_label).replace(QLatin1Char('&'), QStringLiteral("&&")));
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
  if (menu_->isEmpty()) {
    auto* none = menu_->addAction(tr("No companion apps installed"));
    none->setObjectName(QStringLiteral("companionsNoneAction"));
    none->setEnabled(false);
    none->setMenuRole(QAction::NoRole);
    none->setProperty(kCommandSearchDataEntryProperty, true);  // a folder state, not a command
  }
  menu_->addSeparator();
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
  if (McpAttachment::send_companion_command(manifest.id, QStringLiteral("show"))) {
    window_.statusBar()->showMessage(tr("Asked %1 to show its panel").arg(manifest.name), 4000);
    return true;
  }
  const auto program = resolve_companion_command(manifest);
  const auto working_directory = manifest.working_directory.isEmpty()
                                     ? QFileInfo(manifest.manifest_path).absolutePath()
                                     : manifest.working_directory;
  if (program.isEmpty() || !launcher_(program, manifest.args, working_directory, companion_environment(manifest))) {
    window_.statusBar()->showMessage(tr("Could not start %1 (%2)").arg(manifest.name, manifest.command), 8000);
    return false;
  }
  window_.statusBar()->showMessage(tr("Starting %1...").arg(manifest.name), 4000);
  return true;
}

bool CompanionMenu::quit(const CompanionManifest& manifest) {
  if (!McpAttachment::send_companion_command(manifest.id, QStringLiteral("quit"))) {
    return false;
  }
  window_.statusBar()->showMessage(tr("Asked %1 to quit").arg(manifest.name), 4000);
  return true;
}

}  // namespace patchy::ui
