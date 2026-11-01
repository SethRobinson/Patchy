// The GIMP 3 plug-in scan and query cache (gimp_plugin_catalog.hpp).

#include "ui/gimp_plugin_catalog.hpp"

#include "plugins/gimp/gimp_procedure.hpp"
#include "support/path_utils.hpp"
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
#include <set>
#include <thread>

namespace patchy::ui {

namespace {

// Bump when the helper's classification or the JSON it writes changes: every cached
// query is redone once.
constexpr int kCacheVersion = 3;

QString tr_scan(const char* text) { return QCoreApplication::translate("GimpPlugins", text); }

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

// Runs one helper over a list of plug-ins; returns the helper's JSON "plugins" array
// (one object per launch, in order), or an empty array with `error` set.
QJsonArray query_with_helper(const QString& helper, const std::vector<gimp::link::LaunchSpec>& launches, QString* error) {
  QProcess process;
  process.setProgram(helper);
  process.setProcessChannelMode(QProcess::SeparateChannels);
  process.start();
  if (!process.waitForStarted(10000)) {
    *error = tr_scan("The GIMP plug-in helper could not be started.");
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
        *error = tr_scan("The GIMP plug-in helper did not finish.");
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
  *error = tr_scan("The GIMP plug-in helper stopped without an answer.");
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

std::optional<gimp::link::LaunchSpec> gimp_plugin_launch_spec(const gimp::GimpInstall& install, const QString& plugin_file,
                                                             const QString& language, QString* error) {
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
  return launch;
}

GimpPluginScanResult scan_gimp_plugins(const GimpPluginScanOptions& options) {
  GimpPluginScanResult result;
  if (!options.enabled) {
    result.problem = tr_scan("GIMP 3 plug-ins are turned off in Preferences.");
    return result;
  }
  result.install = select_gimp_install(options);
  if (!result.install.has_value()) {
    result.problem = options.install_folder.isEmpty()
                         ? tr_scan("No GIMP 3 installation was found. Install GIMP 3, or choose its folder in Preferences.")
                         : tr_scan("The chosen folder does not contain GIMP 3.");
    return result;
  }
  const auto& install = *result.install;
  const auto helper = gimp_plugin_helper_path();
  if (!QFileInfo::exists(helper)) {
    result.helper_missing = true;
    result.problem = tr_scan("Patchy's GIMP plug-in helper is missing from its program folder.");
    return result;
  }
#ifndef Q_OS_WIN
  if (install.flatpak) {
    result.problem = tr_scan("GIMP's Flatpak was found, but running its plug-ins from Patchy is not supported yet.");
    return result;
  }
#endif

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
    auto launch = gimp_plugin_launch_spec(install, file, options.language, &error);
    if (!launch.has_value()) {
      result.files.push_back({file, false, error, 0, false});
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
          result.files.push_back({file, false, s.error.isEmpty() ? tr_scan("No answer from the helper.") : s.error, 0, false});
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
