// The GIMP 3 plug-in scan and query cache (gimp_plugin_catalog.hpp).

#include "ui/gimp_plugin_catalog.hpp"

#include "plugins/gimp/gimp_flatpak.hpp"
#include "plugins/gimp/gimp_procedure.hpp"
#include "support/path_utils.hpp"
#include "ui/localization.hpp"
#include "ui/qt_paths.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSaveFile>
#include <QStandardPaths>

#include <algorithm>
#include <map>
#include <mutex>
#include <set>
#include <thread>

namespace patchy::ui {

namespace {

// Bump when the helper's classification or the JSON it writes changes: every cached
// query is redone once.
constexpr int kCacheVersion = 6;

std::string utf8(const QString& s) { return s.toStdString(); }

QString cache_key(const QFileInfo& info) {
  return QStringLiteral("%1|%2|%3")
      .arg(info.absoluteFilePath())
      .arg(info.size())
      .arg(info.lastModified().toMSecsSinceEpoch());
}

QStringList menu_from(const QJsonArray& paths) {
  for (const auto& value : paths) {
    const auto segments = gimp::image_menu_segments(utf8(value.toString()));
    if (segments.empty()) {
      continue;
    }
    QStringList out;
    for (std::size_t i = 0; i < segments.size(); ++i) {
      const auto segment = QString::fromStdString(segments[i]).trimmed();
      // "Filters" holds nearly everything; its subfolders are enough of a path.
      if (i == 0 && segment == QLatin1String("Filters") && segments.size() > 1) {
        continue;
      }
      // Placement hints such as "[Artistic]" are not menu names.
      if (segment.startsWith(QLatin1Char('[')) || segment.isEmpty()) {
        continue;
      }
      out << segment;
    }
    return out;
  }
  return {};
}

void add_procedures(const QJsonObject& plugin, const QString& plugin_file, std::vector<GimpPluginProcedure>& out,
                    std::set<QString>& seen) {
  for (const auto& value : plugin.value(QStringLiteral("procedures")).toArray()) {
    const auto p = value.toObject();
    GimpPluginProcedure entry;
    entry.procedure = p.value(QStringLiteral("name")).toString();
    if (entry.procedure.isEmpty() || !seen.insert(entry.procedure).second) {
      continue;  // a procedure of the same name in an earlier folder wins
    }
    entry.identifier = QStringLiteral("gimp.") + entry.procedure;
    entry.label = QString::fromStdString(gimp::strip_mnemonic(utf8(p.value(QStringLiteral("menuLabel")).toString())));
    if (entry.label.isEmpty()) {
      entry.label = entry.procedure;
    }
    entry.menu = menu_from(p.value(QStringLiteral("menuPaths")).toArray());
    entry.blurb = p.value(QStringLiteral("blurb")).toString();
    entry.help = p.value(QStringLiteral("help")).toString();
    entry.authors = p.value(QStringLiteral("authors")).toString();
    entry.use = p.value(QStringLiteral("use")).toString();
    entry.reason = p.value(QStringLiteral("reason")).toString();
    entry.image_types = p.value(QStringLiteral("imageTypes")).toString();
    entry.plugin_file = plugin_file;
    for (const auto& param : p.value(QStringLiteral("params")).toArray()) {
      const auto o = param.toObject();
      entry.params.push_back({o.value(QStringLiteral("name")).toString(), o.value(QStringLiteral("nick")).toString(),
                              o.value(QStringLiteral("blurb")).toString(), o.value(QStringLiteral("kind")).toString(),
                              o.value(QStringLiteral("valueType")).toString()});
    }
    entry.install = QByteArray::fromBase64(p.value(QStringLiteral("install")).toString().toLatin1());
    out.push_back(std::move(entry));
  }
}

#ifdef Q_OS_MACOS
// GIMP.app's own python3 carries a parent launch constraint in its code signature (only
// org.gimp.gimp may start it; observed with GIMP 3.2.6): macOS kills it when anything
// else does. codesign reports the constraint; the answer is kept per program.
bool requires_gimp_as_parent(const QString& program) {
  static std::mutex mutex;
  static std::map<QString, bool> known;
  const std::lock_guard lock(mutex);
  const auto key = program + QLatin1Char('|') + QString::number(QFileInfo(program).lastModified().toMSecsSinceEpoch());
  if (const auto it = known.find(key); it != known.end()) {
    return it->second;
  }
  QProcess codesign;
  codesign.setProcessChannelMode(QProcess::MergedChannels);
  codesign.start(QStringLiteral("/usr/bin/codesign"), {QStringLiteral("-d"), QStringLiteral("-vvvv"), program});
  bool constrained = false;
  if (codesign.waitForFinished(10000)) {
    constrained = codesign.readAll().contains("Has Parent Launch Constraints");
  } else {
    codesign.kill();
    codesign.waitForFinished(1000);
  }
  known[key] = constrained;
  return constrained;
}
#endif

// Plug-ins get the desktop's session bus back when an offscreen Patchy turned its own
// off (src/app/main.cpp): GIMP's libraries and flatpak-spawn use it.
void restore_session_bus(gimp::link::LaunchSpec& launch) {
  if (!qEnvironmentVariableIsSet("PATCHY_DESKTOP_DBUS_ADDRESS")) {
    return;
  }
  const auto address = qEnvironmentVariable("PATCHY_DESKTOP_DBUS_ADDRESS");
  if (address.isEmpty()) {
    launch.unset.emplace_back("DBUS_SESSION_BUS_ADDRESS");
  } else {
    launch.environment.push_back("DBUS_SESSION_BUS_ADDRESS=" + utf8(address));
  }
}

// Runs one helper over a list of plug-ins; returns the helper's JSON "plugins" array
// (one object per launch, in order), or an empty array with `error` set.
QJsonArray query_with_helper(const QString& helper, const std::vector<gimp::link::LaunchSpec>& launches, QString* error) {
  QProcess process;
  process.setProgram(helper);
  process.setProcessChannelMode(QProcess::SeparateChannels);
  process.start();
  if (!process.waitForStarted(10000)) {
    *error = QCoreApplication::translate("GimpPlugins", "The GIMP plug-in helper could not be started.");
    return {};
  }
  gimp::link::QueryRequest request;
  request.plugins = launches;
  request.timeout_ms = 30000;
  const auto frame = gimp::link::encode(request);
  process.write(reinterpret_cast<const char*>(frame.data()), static_cast<qint64>(frame.size()));
  gimp::link::FrameBuffer buffer;
  QElapsedTimer timer;
  timer.start();
  const qint64 limit = 30000LL * static_cast<qint64>(launches.size()) + 15000;
  for (;;) {
    if (process.bytesAvailable() == 0 && !process.waitForReadyRead(500)) {
      if (process.state() == QProcess::NotRunning) {
        break;
      }
      if (timer.elapsed() > limit) {
        process.kill();
        process.waitForFinished(3000);
        *error = QCoreApplication::translate("GimpPlugins", "The GIMP plug-in helper did not finish.");
        return {};
      }
      continue;
    }
    const auto bytes = process.readAllStandardOutput();
    buffer.append(reinterpret_cast<const std::uint8_t*>(bytes.constData()), static_cast<std::size_t>(bytes.size()));
    try {
      while (auto f = buffer.next()) {
        if (f->first == gimp::link::Frame::QueryResult) {
          const auto json = gimp::link::decode_text(f->second);
          process.closeWriteChannel();
          process.waitForFinished(5000);
          const auto doc = QJsonDocument::fromJson(QByteArray::fromStdString(json));
          return doc.object().value(QStringLiteral("plugins")).toArray();
        }
      }
    } catch (const gimp::link::LinkError&) {
      break;
    }
  }
  process.kill();
  process.waitForFinished(3000);
  *error = QCoreApplication::translate("GimpPlugins", "The GIMP plug-in helper stopped without an answer.");
  return {};
}

}  // namespace

QString gimp_plugin_helper_path() {
  const QDir dir(QCoreApplication::applicationDirPath());
#ifdef Q_OS_WIN
  return dir.filePath(QStringLiteral("patchy-gimp-host.exe"));
#else
  return dir.filePath(QStringLiteral("patchy-gimp-host"));
#endif
}

QString gimp_plugin_display_name(const QString& label) {
  auto name = label.trimmed();
  while (name.endsWith(QLatin1Char('.')) || name.endsWith(QChar(0x2026))) {
    name.chop(1);
  }
  return name.trimmed();
}

QString gimp_plugin_language() {
  const auto code = LocalizationManager::instance().current_language();
  return code.isEmpty() ? QStringLiteral("en") : code;
}

QString gimp_plugin_profile_dir() {
  const auto base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  return QDir(base).filePath(QStringLiteral("gimp-plugins/profile"));
}

std::optional<gimp::GimpInstall> select_gimp_install(const GimpPluginScanOptions& options) {
  if (!options.install_folder.isEmpty()) {
    return gimp::gimp_install_from_folder(to_filesystem_path(options.install_folder));
  }
  auto found = gimp::detect_gimp_installs();
  if (found.empty()) {
    return std::nullopt;
  }
  return found.front();
}

namespace {

// GIMP's Flatpak: the plug-in starts in GIMP's sandbox (gimp_flatpak.hpp).
std::optional<gimp::link::LaunchSpec> flatpak_plugin_launch_spec(const gimp::GimpInstall& install, const QString& plugin_file,
                                                                 const QString& language, QString* error) {
  const auto interpreters = gimp::load_interpreters({install.lib_dir / "interpreters", install.user_dir / "interpreters"});
  std::string why;
  const auto command = gimp::flatpak_plugin_command(install, to_filesystem_path(plugin_file), interpreters, &why);
  if (!command.has_value()) {
    if (error != nullptr) {
      *error = QString::fromStdString(why);
    }
    return std::nullopt;
  }
  gimp::FlatpakLaunchOptions options;
  options.from_flatpak = gimp::running_in_flatpak();
  options.program = utf8(options.from_flatpak ? QStringLiteral("/usr/bin/flatpak-spawn")
                                              : QStandardPaths::findExecutable(QStringLiteral("flatpak")));
  if (options.program.empty()) {
    if (error != nullptr) {
      *error = QCoreApplication::translate("GimpPlugins", "the flatpak command was not found");
    }
    return std::nullopt;
  }
  const auto profile = gimp_plugin_profile_dir();
  QDir().mkpath(profile);
  options.shared_folders.push_back(to_filesystem_path(profile));
  // The environ files name sandbox paths through ${gimp_installation_dir} (/app).
  for (const auto& [name, value] : gimp::load_environ({install.lib_dir / "environ", install.user_dir / "environ"}, "/app")) {
    options.environment.push_back(name + "=" + value);
  }
  options.environment.push_back("GIMP3_DIRECTORY=" + utf8(profile));
  if (!language.isEmpty()) {
    options.environment.push_back("LANGUAGE=" + utf8(language));
    options.environment.push_back("LANG=" + utf8(language));
  }
  options.environment.push_back("PYTHONIOENCODING=utf-8");
  auto launch = gimp::flatpak_launch(install, *command, options);
  launch.data_dir = path_to_utf8(gimp::flatpak_sandbox_path(install, install.data_dir));
  launch.profile_dir = utf8(profile);
  // The helper's own environment reaches the flatpak command, never the sandbox's
  // programs (flatpak sets those up itself).
  launch.unset = {"LD_LIBRARY_PATH", "LD_PRELOAD", "PYTHONHOME", "PYTHONPATH"};
  restore_session_bus(launch);
  return launch;
}

}  // namespace

QString gimp_plugin_flatpak_problem(const gimp::GimpInstall& install) {
  if (!install.flatpak || !gimp::running_in_flatpak()) {
    return {};
  }
  QFile info(QStringLiteral("/.flatpak-info"));
  if (info.open(QIODevice::ReadOnly) && gimp::flatpak_info_allows_host_commands(info.readAll().toStdString())) {
    return {};
  }
  return QCoreApplication::translate("GimpPlugins", "GIMP's Flatpak was found, but this copy of Patchy may not start programs outside its own sandbox. "
                 "Give it that permission with: flatpak override --user --talk-name=org.freedesktop.Flatpak com.rtsoft.patchy");
}

std::optional<gimp::link::LaunchSpec> gimp_plugin_launch_spec(const gimp::GimpInstall& install, const QString& plugin_file,
                                                             const QString& language, QString* error,
                                                             bool* launch_constrained) {
  if (launch_constrained != nullptr) {
    *launch_constrained = false;
  }
  if (install.flatpak) {
    return flatpak_plugin_launch_spec(install, plugin_file, language, error);
  }
  const auto interpreters = gimp::load_interpreters({install.lib_dir / "interpreters", install.user_dir / "interpreters"});
  const auto environment = gimp::load_environ({install.lib_dir / "environ", install.user_dir / "environ"}, install.prefix);
  auto search = gimp::environ_search_path(environment);
  search.push_back(install.bin_dir);
  std::string why;
  const auto command = gimp::resolve_plugin_command(to_filesystem_path(plugin_file), interpreters, search, &why);
  if (!command.has_value()) {
    if (error != nullptr) {
      *error = QString::fromStdString(why);
    }
    return std::nullopt;
  }
#ifdef Q_OS_MACOS
  if (!command->interpreter.empty() && requires_gimp_as_parent(to_qstring(command->program))) {
    if (launch_constrained != nullptr) {
      *launch_constrained = true;
    }
    if (error != nullptr) {
      *error = QCoreApplication::translate("GimpPlugins", "macOS lets only GIMP itself start %1 (a launch constraint in GIMP's code signature)")
                   .arg(to_qstring(command->program.filename()));
    }
    return std::nullopt;
  }
#endif
  gimp::link::LaunchSpec launch;
  launch.program = path_to_utf8(command->program);
  launch.leading_args = command->leading_args;
  launch.working_dir = utf8(QFileInfo(plugin_file).absolutePath());
  launch.data_dir = path_to_utf8(install.data_dir);
  const auto profile = gimp_plugin_profile_dir();
  QDir().mkpath(profile);
  launch.profile_dir = utf8(QDir::toNativeSeparators(profile));
  for (const auto& [name, value] : environment) {
    launch.environment.push_back(name + "=" + value);
  }
  launch.environment.push_back("GIMP3_DIRECTORY=" + launch.profile_dir);
  const auto schemas = install.prefix / "share" / "glib-2.0" / "schemas";
  if (QFileInfo(to_qstring(schemas)).isDir()) {
    launch.environment.push_back("GSETTINGS_SCHEMA_DIR=" + path_to_utf8(schemas));
  }
  if (!language.isEmpty()) {
    launch.environment.push_back("LANGUAGE=" + utf8(language));
    launch.environment.push_back("LANG=" + utf8(language));
  }
  launch.environment.push_back("PYTHONIOENCODING=utf-8");
  // A Python of the user's own must not leak into GIMP's bundled one.
  launch.unset = {"PYTHONHOME", "PYTHONPATH", "PYTHONSTARTUP", "GI_TYPELIB_PATH", "GTK_THEME", "GTK_PATH"};
#ifndef Q_OS_WIN
  // An AppImage points the loader and Qt at its own libraries; GIMP's plug-ins need the
  // system's.
  for (const char* name : {"LD_LIBRARY_PATH", "LD_PRELOAD", "QT_PLUGIN_PATH", "QT_QPA_PLATFORM_PLUGIN_PATH",
                           "GDK_PIXBUF_MODULE_FILE", "GDK_PIXBUF_MODULEDIR", "GIO_MODULE_DIR", "DYLD_LIBRARY_PATH",
                           "DYLD_FRAMEWORK_PATH"}) {
    launch.unset.emplace_back(name);
  }
#endif
  restore_session_bus(launch);
  return launch;
}

GimpPluginScanResult scan_gimp_plugins(const GimpPluginScanOptions& options) {
  GimpPluginScanResult result;
  if (!options.enabled) {
    result.problem = QCoreApplication::translate("GimpPlugins", "GIMP 3 plug-ins are turned off in Preferences.");
    return result;
  }
  result.install = select_gimp_install(options);
  if (!result.install.has_value()) {
    result.problem = options.install_folder.isEmpty()
                         ? QCoreApplication::translate("GimpPlugins", "No GIMP 3 installation was found. Install GIMP 3, or choose its folder in Preferences.")
                         : QCoreApplication::translate("GimpPlugins", "The chosen folder does not contain GIMP 3.");
    return result;
  }
  const auto& install = *result.install;
  const auto helper = gimp_plugin_helper_path();
  if (!QFileInfo::exists(helper)) {
    result.helper_missing = true;
    result.problem = QCoreApplication::translate("GimpPlugins", "Patchy's GIMP plug-in helper is missing from its program folder.");
    return result;
  }
  if (const auto problem = gimp_plugin_flatpak_problem(install); !problem.isEmpty()) {
    result.problem = problem;
    return result;
  }

  // Folders, most specific first: a plug-in in an extra or user folder replaces a
  // system one of the same name.
  for (const auto& folder : options.extra_folders) {
    result.folders << QDir::cleanPath(folder);
  }
  std::vector<std::filesystem::path> install_folders =
      options.only_extra_folders ? std::vector<std::filesystem::path>{} : install.plugin_folders;
  if (!install_folders.empty()) {
    std::rotate(install_folders.begin(), install_folders.begin() + 1, install_folders.end());  // system last
  }
  for (const auto& folder : install_folders) {
    result.folders << QDir::cleanPath(to_qstring(folder));
  }
  result.folders.removeDuplicates();

  const auto interpreters = gimp::load_interpreters({install.lib_dir / "interpreters", install.user_dir / "interpreters"});
  std::vector<QString> files;
  std::set<QString> names;
  for (const auto& folder : result.folders) {
    for (const auto& file : gimp::find_plugin_files(to_filesystem_path(folder), interpreters)) {
      const auto path = to_qstring(file);
      if (names.insert(QFileInfo(path).completeBaseName().toLower()).second) {
        files.push_back(path);
      }
    }
  }

  // The cache: one entry per plug-in file, reused while size and file time match.
  const auto cache_path = options.cache_file.isEmpty()
                              ? QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
                                    .filePath(QStringLiteral("gimp-plugins/query-cache.json"))
                              : options.cache_file;
  QJsonObject cache;
  {
    QFile in(cache_path);
    if (in.open(QIODevice::ReadOnly)) {
      const auto doc = QJsonDocument::fromJson(in.readAll()).object();
      if (doc.value(QStringLiteral("version")).toInt() == kCacheVersion &&
          doc.value(QStringLiteral("language")).toString() == options.language &&
          doc.value(QStringLiteral("gimp")).toString() == QString::fromStdString(install.version)) {
        cache = doc.value(QStringLiteral("files")).toObject();
      }
    }
  }
  QJsonObject fresh_cache;
  std::map<QString, QJsonObject> plugin_json;  // by file
  std::vector<QString> to_query;
  std::vector<gimp::link::LaunchSpec> launches;
  for (const auto& file : files) {
    const QFileInfo info(file);
    const auto key = cache_key(info);
    if (!options.force_requery && cache.contains(key)) {
      plugin_json[file] = cache.value(key).toObject();
      fresh_cache.insert(key, plugin_json[file]);
      continue;
    }
    QString error;
    bool constrained = false;
    auto launch = gimp_plugin_launch_spec(install, file, options.language, &error, &constrained);
    if (!launch.has_value()) {
      result.files.push_back({file, false, error, 0, false, constrained});
      continue;
    }
    to_query.push_back(file);
    launches.push_back(std::move(*launch));
  }

  // Query in parallel helpers, each over a slice.
  if (!launches.empty()) {
    const int helpers = std::clamp(options.parallel_helpers, 1, 8);
    const std::size_t slice = (launches.size() + static_cast<std::size_t>(helpers) - 1) / static_cast<std::size_t>(helpers);
    struct Running {
      std::size_t begin{0};
      std::size_t end{0};
      QJsonArray answer;
      QString error;
    };
    std::vector<Running> slices;
    for (std::size_t begin = 0; begin < launches.size(); begin += slice) {
      slices.push_back({begin, std::min(launches.size(), begin + slice), {}, {}});
    }
    std::vector<std::thread> threads;
    for (auto& s : slices) {
      threads.emplace_back([&s, &launches, &helper] {
        std::vector<gimp::link::LaunchSpec> part(launches.begin() + static_cast<std::ptrdiff_t>(s.begin),
                                                 launches.begin() + static_cast<std::ptrdiff_t>(s.end));
        s.answer = query_with_helper(helper, part, &s.error);
      });
    }
    for (auto& t : threads) {
      t.join();
    }
    for (const auto& s : slices) {
      for (std::size_t i = s.begin; i < s.end; ++i) {
        const auto& file = to_query[i];
        const auto index = static_cast<qsizetype>(i - s.begin);
        if (index >= s.answer.size()) {
          result.files.push_back({file, false, s.error.isEmpty() ? QCoreApplication::translate("GimpPlugins", "No answer from the helper.") : s.error, 0, false});
          continue;
        }
        const auto object = s.answer.at(index).toObject();
        plugin_json[file] = object;
        // Failed queries are cached too, so a broken plug-in is not restarted on every
        // launch; a changed file is queried again.
        fresh_cache.insert(cache_key(QFileInfo(file)), object);
      }
    }
  }

  std::set<QString> seen;
  for (const auto& file : files) {
    const auto it = plugin_json.find(file);
    if (it == plugin_json.end()) {
      continue;
    }
    const auto& object = it->second;
    GimpPluginFileReport report;
    report.path = file;
    report.ok = object.value(QStringLiteral("ok")).toBool();
    report.error = object.value(QStringLiteral("error")).toString();
    report.procedures = static_cast<int>(object.value(QStringLiteral("procedures")).toArray().size());
    report.from_cache = std::find(to_query.begin(), to_query.end(), file) == to_query.end();
    result.files.push_back(report);
    add_procedures(object, file, result.procedures, seen);
  }

  QDir().mkpath(QFileInfo(cache_path).absolutePath());
  QSaveFile out(cache_path);
  if (out.open(QIODevice::WriteOnly)) {
    QJsonObject doc;
    doc.insert(QStringLiteral("version"), kCacheVersion);
    doc.insert(QStringLiteral("language"), options.language);
    doc.insert(QStringLiteral("gimp"), QString::fromStdString(install.version));
    doc.insert(QStringLiteral("files"), fresh_cache);
    out.write(QJsonDocument(doc).toJson(QJsonDocument::Compact));
    out.commit();
  }
  std::sort(result.procedures.begin(), result.procedures.end(), [](const auto& a, const auto& b) {
    return a.procedure < b.procedure;
  });
  return result;
}

}  // namespace patchy::ui
