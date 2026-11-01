// GEGL filters from the user's GIMP 3 (docs/gegl-filters.md): the scan through
// patchy-gegl-host and the FilterDefinitions that run one operation through it.

#include "ui/gegl_filters.hpp"

#include "core/layer.hpp"
#include "core/pixel_buffer.hpp"
#include "plugins/gimp/gimp_flatpak.hpp"
#include "ui/qt_paths.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSaveFile>
#include <QStandardPaths>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>

namespace patchy::ui {

namespace {

// Categories that are not image filters: graph plumbing, file and video I/O, geometry,
// compositing (two inputs), vectors and debugging aids.
const std::set<QString> kExcludedCategories = {
    QStringLiteral("core"),        QStringLiteral("programming"), QStringLiteral("hidden"),
    QStringLiteral("meta"),        QStringLiteral("json"),        QStringLiteral("video"),
    QStringLiteral("compositors"), QStringLiteral("blend"),       QStringLiteral("transform"),
    QStringLiteral("vector"),      QStringLiteral("svgfilter"),   QStringLiteral("input"),
    QStringLiteral("output"),      QStringLiteral("debug"),
};

// Left out by name: the families docs/legal-constraints.md and its patent records keep out
// of Patchy, whatever their category (the triage is in docs/gegl-filters.md, "Legal review").
// GEGL's source is never read, so an operation whose method cannot be checked from its own
// description against a recorded boundary stays out.
bool excluded_by_name(const QString& name) {
  static const QStringList kExcluded = {
      QStringLiteral("gegl:median-blur"),         // Surface Blur rule: histogram form unverifiable
      QStringLiteral("gegl:focus-blur"),          // Blur Gallery record: a per-pixel blur radius mask
      QStringLiteral("gegl:slic"),                // superpixels: not reviewed
      QStringLiteral("gegl:waterpixels"),         // superpixels: not reviewed
      QStringLiteral("gegl:shadows-highlights"),  // Shadow/Highlight adjustment family: not reviewed
  };
  return kExcluded.contains(name) || name.contains(QLatin1String("inpaint")) ||
         name.contains(QLatin1String("seamless-clone")) || name.contains(QLatin1String("red-eye")) ||
         name.contains(QLatin1String("heal"));
}

// GEGL's limits can be the whole double range; the dialog's fields need something sane.
constexpr double kLargestValue = 1.0e6;

// What a run needs to know about each setting it may send.
struct GeglSetting {
  std::string key;
  char kind{'d'};                         // the helper's set kinds: d f i u b e
  std::map<std::string, int> enum_values;  // choice nick -> GEGL value
};

struct GeglOperationSpec {
  std::string operation;   // "gegl:cartoon"
  std::vector<GeglSetting> settings;
};

QString babl_format_for(const PixelFormat& format) {
  if (format.color_mode != ColorMode::RGB || (format.channels != 3 && format.channels != 4)) {
    return {};
  }
  const bool alpha = format.channels == 4;
  switch (format.bit_depth) {
    case BitDepth::UInt8:
      return alpha ? QStringLiteral("R'G'B'A u8") : QStringLiteral("R'G'B' u8");
    case BitDepth::UInt16:
      return alpha ? QStringLiteral("R'G'B'A u16") : QStringLiteral("R'G'B' u16");
    case BitDepth::Float32:  // Patchy's 32-bit pixels are linear
      return alpha ? QStringLiteral("RGBA float") : QStringLiteral("RGB float");
  }
  return {};
}

QString number_text(double value) { return QString::number(value, 'g', 17); }

// The helper's environment. macOS: GIMP's libraries name each other through an @rpath
// only GIMP's own programs set, so the loader is pointed at GIMP's lib folder (the signed
// helper carries the entitlement for it, packaging/macos/patchy-gegl-host.entitlements).
// LANGUAGE: GEGL translates its own labels through gettext (the helper asks its catalog).
// `flatpak run` passes the environment into GIMP's sandbox.
void prepare_helper(QProcess& helper, const GeglHelperCommand& command, const QString& language = {}) {
  auto environment = QProcessEnvironment::systemEnvironment();
  const auto library_folder = QFileInfo(command.library).absolutePath();
#ifdef Q_OS_MACOS
  environment.insert(QStringLiteral("DYLD_LIBRARY_PATH"), library_folder);
#endif
#ifndef Q_OS_WIN
  // Patchy's own GEGL off Windows: its libraries and modules where the bundle keeps them
  // (GEGL_PATH and BABL_PATH are GEGL's and babl's documented module paths). Windows GEGL
  // finds them relative to its DLL.
  if (command.bundled) {
#ifndef Q_OS_MACOS
    const auto previous = environment.value(QStringLiteral("LD_LIBRARY_PATH"));
    environment.insert(QStringLiteral("LD_LIBRARY_PATH"),
                       previous.isEmpty() ? library_folder : library_folder + QLatin1Char(':') + previous);
#endif
    environment.insert(QStringLiteral("GEGL_PATH"), QDir(library_folder).filePath(QStringLiteral("gegl-0.4")));
    environment.insert(QStringLiteral("BABL_PATH"), QDir(library_folder).filePath(QStringLiteral("babl-0.1")));
  }
#endif
  if (!language.isEmpty()) {
    environment.insert(QStringLiteral("LANGUAGE"), language);
  }
  helper.setProcessEnvironment(environment);
}

// A GEGL color setting is a choice: GEGL's own default, Patchy's foreground or background
// color, black or white. The labels are Patchy's existing data texts.
struct ColorChoice {
  const char* value;
  const char* label;
};
constexpr ColorChoice kColorChoices[] = {
    {"default", QT_TRANSLATE_NOOP("QObject", "Default")},
    {"foreground", QT_TRANSLATE_NOOP("QObject", "Foreground Color")},
    {"background", QT_TRANSLATE_NOOP("QObject", "Background Color")},
    {"black", QT_TRANSLATE_NOOP("QObject", "Black")},
    {"white", QT_TRANSLATE_NOOP("QObject", "White")},
};

// The helper's "c" value for a color choice; empty for GEGL's default (nothing is sent).
QByteArray color_setting(const std::string& choice, const FilterInvocation& invocation) {
  const auto rgb = [](const RgbColor& c) {
    return QByteArray::number(c.red / 255.0) + ' ' + QByteArray::number(c.green / 255.0) + ' ' +
           QByteArray::number(c.blue / 255.0) + " 1";
  };
  if (choice == "foreground") return rgb(invocation.foreground);
  if (choice == "background") return rgb(invocation.background);
  if (choice == "black") return "0 0 0 1";
  if (choice == "white") return "1 1 1 1";
  return {};
}

// PATCHY_GEGL_TRACE=1: each run's steps on stderr.
void trace(const char* step, const GeglOperationSpec& spec, qsizetype bytes) {
  static const bool enabled = qEnvironmentVariableIntValue("PATCHY_GEGL_TRACE") != 0;
  if (enabled) {
    std::fprintf(stderr, "gegl: %s %s (%lld bytes)\n", step, spec.operation.c_str(), static_cast<long long>(bytes));
    std::fflush(stderr);
  }
}

void run_operation(const GeglHelperCommand& command, const GeglOperationSpec& spec, const FilterInvocation& invocation,
                   PixelBuffer& pixels, const FilterProgress* progress) {
  const auto format = babl_format_for(pixels.format());
  if (format.isEmpty()) {
    throw std::runtime_error(
        QCoreApplication::translate("GeglFilters", "GEGL filters run on RGB layers only.").toStdString());
  }
  if (pixels.width() <= 0 || pixels.height() <= 0) {
    return;
  }
  QByteArray request;
  request += "op " + QByteArray::fromStdString(spec.operation) + '\n';
  request += "size " + QByteArray::number(pixels.width()) + ' ' + QByteArray::number(pixels.height()) + '\n';
  request += "format " + format.toUtf8() + '\n';
  for (const auto& setting : spec.settings) {
    const auto found = invocation.parameters.find(setting.key);
    if (found == invocation.parameters.end()) {
      continue;  // a hidden setting keeps GEGL's default
    }
    QByteArray value;
    const auto& v = found->second;
    if (setting.kind == 'c') {
      const auto* choice = std::get_if<std::string>(&v);
      value = choice != nullptr ? color_setting(*choice, invocation) : QByteArray();
      if (value.isEmpty()) {
        continue;
      }
    } else if (setting.kind == 'e') {
      const auto* nick = std::get_if<std::string>(&v);
      const auto it = nick != nullptr ? setting.enum_values.find(*nick) : setting.enum_values.end();
      if (it == setting.enum_values.end()) {
        continue;
      }
      value = QByteArray::number(it->second);
    } else if (const auto* b = std::get_if<bool>(&v)) {
      value = *b ? "1" : "0";
    } else if (const auto* i = std::get_if<std::int64_t>(&v)) {
      value = QByteArray::number(static_cast<qlonglong>(*i));
    } else if (const auto* d = std::get_if<double>(&v)) {
      value = setting.kind == 'd' || setting.kind == 'f' ? number_text(*d).toUtf8()
                                                          : QByteArray::number(static_cast<qlonglong>(std::llround(*d)));
    } else {
      continue;
    }
    request += "set " + QByteArray(1, setting.kind) + ' ' + QByteArray::fromStdString(setting.key) + ' ' + value + '\n';
  }
  request += "end\n";
  const auto row_bytes = static_cast<std::size_t>(pixels.width()) * bytes_per_pixel(pixels.format());
  const auto stride = pixels.stride_bytes();
  const auto rows = static_cast<std::size_t>(pixels.height());
  {
    const auto source = std::as_const(pixels).data();
    const auto header = request.size();
    request.resize(static_cast<qsizetype>(static_cast<std::size_t>(header) + row_bytes * rows));
    for (std::size_t y = 0; y < rows; ++y) {
      std::memcpy(request.data() + header + static_cast<qsizetype>(y * row_bytes), source.data() + y * stride, row_bytes);
    }
  }

  const auto cancelled = [progress] {
    return progress != nullptr && progress->update && !progress->update(0, 1, FilterProgressStage::Filtering);
  };
  trace("start", spec, request.size());
  QProcess helper;
  prepare_helper(helper, command);
  helper.setProgram(command.program);
  helper.setArguments(command.arguments + QStringList{QStringLiteral("--gpl3"), QStringLiteral("--run")});
  helper.start();
  if (!helper.waitForStarted(10000)) {
    throw std::runtime_error(
        QCoreApplication::translate("GeglFilters", "Patchy's GEGL helper could not be started.").toStdString());
  }
  trace("started", spec, 0);
  // Runs happen on workers without an event loop: only the waitFor calls move the bytes.
  helper.write(request);
  QByteArray output;
  while (helper.bytesToWrite() > 0 && helper.state() == QProcess::Running) {
    helper.waitForBytesWritten(100);
    output += helper.readAllStandardOutput();
    if (cancelled()) {
      helper.kill();
      helper.waitForFinished(2000);
      throw FilterCancelled();
    }
  }
  helper.closeWriteChannel();
  trace("written", spec, 0);
  while (!helper.waitForFinished(100)) {
    output += helper.readAllStandardOutput();
    if (helper.state() == QProcess::NotRunning) {
      break;
    }
    if (cancelled()) {
      helper.kill();
      helper.waitForFinished(2000);
      throw FilterCancelled();
    }
  }
  output += helper.readAllStandardOutput();
  trace("finished", spec, output.size());
  if (helper.exitStatus() != QProcess::NormalExit || helper.exitCode() != 0 ||
      static_cast<std::size_t>(output.size()) != row_bytes * rows) {
    const auto detail = QString::fromUtf8(helper.readAllStandardError()).trimmed().left(800);
    //: %1 is GEGL's operation name, %2 what the helper reported.
    throw std::runtime_error(QCoreApplication::translate("GeglFilters", "The GEGL filter %1 failed. %2")
                                 .arg(QString::fromStdString(spec.operation), detail)
                                 .toStdString());
  }
  auto target = pixels.data();
  for (std::size_t y = 0; y < rows; ++y) {
    std::memcpy(target.data() + y * stride, output.constData() + static_cast<qsizetype>(y * row_bytes), row_bytes);
  }
}

// One GEGL property as a Patchy filter parameter; nullopt for a kind the dialog cannot
// show (colors, text, paths), which then keeps GEGL's default.
std::optional<FilterParameterDefinition> parameter_for(const QJsonObject& p, GeglSetting& setting) {
  const auto kind = p.value(QStringLiteral("kind")).toString();
  FilterParameterDefinition d;
  d.key = p.value(QStringLiteral("name")).toString().toStdString();
  d.display_name = p.value(QStringLiteral("label")).toString().toStdString();
  if (d.display_name.empty()) {
    d.display_name = d.key;
  }
  d.control_object_name = "geglParam_" + d.key;
  setting.key = d.key;
  const auto clamp_limit = [](double v) { return std::clamp(v, -kLargestValue, kLargestValue); };
  const auto unit = p.value(QStringLiteral("unit")).toString();
  if (unit == QLatin1String("pixel-distance") || unit == QLatin1String("pixel-coordinate")) {
    d.unit = FilterParameterUnit::Pixels;
    d.spatial_scale = FilterSpatialScale::Pixels;
  } else if (unit == QLatin1String("degree")) {
    d.unit = FilterParameterUnit::Degrees;
  }
  const auto set_range = [&](bool integer) {
    const double minimum = clamp_limit(p.value(QStringLiteral("min")).toDouble());
    const double maximum = clamp_limit(p.value(QStringLiteral("max")).toDouble());
    d.minimum = minimum;
    d.maximum = std::max(minimum, maximum);
    if (p.contains(QStringLiteral("uiMin")) && p.contains(QStringLiteral("uiMax"))) {
      const double ui_minimum = std::clamp(p.value(QStringLiteral("uiMin")).toDouble(), *d.minimum, *d.maximum);
      const double ui_maximum = std::clamp(p.value(QStringLiteral("uiMax")).toDouble(), ui_minimum, *d.maximum);
      d.practical_minimum = ui_minimum;
      d.practical_maximum = ui_maximum;
    }
    if (!integer) {
      const auto digits = p.value(QStringLiteral("digits")).toInt(-1);
      const double span = d.practical_maximum.value_or(*d.maximum) - d.practical_minimum.value_or(*d.minimum);
      d.step = digits >= 0 ? std::pow(10.0, -std::min(digits, 4)) : (span <= 2.0 ? 0.01 : 0.1);
    }
  };
  if (kind == QLatin1String("double") || kind == QLatin1String("float")) {
    setting.kind = kind == QLatin1String("float") ? 'f' : 'd';
    d.kind = FilterParameterKind::Double;
    set_range(false);
    d.default_value = std::clamp(p.value(QStringLiteral("default")).toDouble(), *d.minimum, *d.maximum);
    return d;
  }
  if (kind == QLatin1String("int") || kind == QLatin1String("uint") || kind == QLatin1String("seed")) {
    setting.kind = kind == QLatin1String("int") ? 'i' : 'u';
    d.kind = FilterParameterKind::Integer;
    set_range(true);
    if (kind != QLatin1String("int")) {
      d.minimum = std::max(0.0, *d.minimum);
      d.maximum = std::min(2147483647.0, *d.maximum);
    }
    if (kind == QLatin1String("seed")) {
      d.presentation = FilterParameterPresentation::RandomSeed;
    }
    d.default_value = static_cast<std::int64_t>(
        std::clamp(p.value(QStringLiteral("default")).toDouble(), *d.minimum, *d.maximum));
    return d;
  }
  if (kind == QLatin1String("bool")) {
    setting.kind = 'b';
    d.kind = FilterParameterKind::Boolean;
    d.default_value = p.value(QStringLiteral("default")).toBool();
    return d;
  }
  if (kind == QLatin1String("color")) {
    setting.kind = 'c';
    d.kind = FilterParameterKind::Option;
    for (const auto& choice : kColorChoices) {
      d.options.push_back({choice.value, choice.label});
    }
    d.default_value = std::string("default");
    return d;
  }
  if (kind == QLatin1String("enum")) {
    setting.kind = 'e';
    d.kind = FilterParameterKind::Option;
    const int default_value = p.value(QStringLiteral("default")).toInt();
    std::string default_nick;
    for (const auto& c : p.value(QStringLiteral("choices")).toArray()) {
      const auto choice = c.toObject();
      const auto nick = choice.value(QStringLiteral("nick")).toString().toStdString();
      const int value = choice.value(QStringLiteral("value")).toInt();
      if (nick.empty() || setting.enum_values.contains(nick)) {
        continue;
      }
      setting.enum_values.emplace(nick, value);
      auto label = choice.value(QStringLiteral("label")).toString().toStdString();
      d.options.push_back({nick, label.empty() ? nick : label});
      if (value == default_value && default_nick.empty()) {
        default_nick = nick;
      }
    }
    if (d.options.empty()) {
      return std::nullopt;
    }
    d.default_value = default_nick.empty() ? d.options.front().value : default_nick;
    return d;
  }
  return std::nullopt;
}

}  // namespace

QString gegl_helper_path() {
  const QDir dir(QCoreApplication::applicationDirPath());
#ifdef Q_OS_WIN
  return dir.filePath(QStringLiteral("patchy-gegl-host.exe"));
#else
  return dir.filePath(QStringLiteral("patchy-gegl-host"));
#endif
}

QString bundled_gegl_library() {
  const QDir program(QCoreApplication::applicationDirPath());
  QStringList candidates;
#if defined(Q_OS_WIN)
  candidates << program.filePath(QStringLiteral("gegl/bin/libgegl-0.4-0.dll"));
#elif defined(Q_OS_MACOS)
  candidates << program.filePath(QStringLiteral("../Frameworks/gegl/lib/libgegl-0.4.0.dylib"))
             << program.filePath(QStringLiteral("gegl/lib/libgegl-0.4.0.dylib"));
#else
  candidates << program.filePath(QStringLiteral("gegl/lib/libgegl-0.4.so.0"))
             << program.filePath(QStringLiteral("../lib/patchy/gegl/lib/libgegl-0.4.so.0"));
  if (gimp::running_in_flatpak()) {
    candidates << QStringLiteral("/app/lib/libgegl-0.4.so.0");  // the Flatpak's own gegl module
  }
#endif
  for (const auto& candidate : candidates) {
    if (QFileInfo::exists(candidate)) {
      return QDir::cleanPath(candidate);
    }
  }
  return {};
}

GeglHelperCommand bundled_gegl_command(const QString& library) {
  GeglHelperCommand command;
  command.library = library;
  command.library_file = library;
  command.program = gegl_helper_path();
  command.arguments = {QStringLiteral("--gegl"), library};
  command.bundled = true;
  return command;
}

QString find_gegl_library(const gimp::GimpInstall& install) {
  if (install.flatpak) {
    // Inside GIMP's sandbox its files folder is /app. From Patchy's own Flatpak the
    // helper would have to reach GIMP's sandbox through the host: not done.
    if (gimp::running_in_flatpak() || !QFileInfo::exists(to_qstring(install.prefix / "lib" / "libgegl-0.4.so.0"))) {
      return {};
    }
    return QStringLiteral("/app/lib/libgegl-0.4.so.0");
  }
  QStringList candidates;
#if defined(Q_OS_WIN)
  candidates << to_qstring(install.bin_dir / "libgegl-0.4-0.dll");
#elif defined(Q_OS_MACOS)
  candidates << to_qstring(install.prefix / "lib" / "libgegl-0.4.0.dylib");
#else
  for (const auto* lib : {"lib", "lib64"}) {
    candidates << to_qstring(install.prefix / lib / "libgegl-0.4.so.0");
  }
  // A multiarch layout keeps it beside lib/<triplet>/gimp.
  candidates << to_qstring(install.lib_dir.parent_path().parent_path() / "libgegl-0.4.so.0");
#endif
  for (const auto& candidate : candidates) {
    if (QFileInfo::exists(candidate)) {
      return QDir::cleanPath(candidate);
    }
  }
  return {};
}

GeglHelperCommand gegl_helper_command(const gimp::GimpInstall& install, const QString& library) {
  GeglHelperCommand command;
  command.library = library;
  command.library_file = install.flatpak ? to_qstring(install.prefix / "lib" / "libgegl-0.4.so.0") : library;
  auto helper = gegl_helper_path();
  if (!install.flatpak) {
    command.program = helper;
    command.arguments = {QStringLiteral("--gegl"), library};
    return command;
  }
  // GIMP's Flatpak: the helper runs in GIMP's sandbox, which sees the helper's folder
  // read-only. Flatpak will not share a folder under /usr, so a helper installed there is
  // copied to Patchy's data folder first.
  command.flatpak = true;
  if (helper.startsWith(QLatin1String("/usr/"))) {
    const QDir data(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));
    const auto copy = data.filePath(QStringLiteral("gegl-helper/patchy-gegl-host"));
    const QFileInfo source(helper);
    const QFileInfo target(copy);
    if (!target.exists() || target.size() != source.size() || target.lastModified() < source.lastModified()) {
      QDir().mkpath(target.absolutePath());
      QFile::remove(copy);
      QFile::copy(helper, copy);
    }
    helper = copy;
  }
  const auto flatpak = QStandardPaths::findExecutable(QStringLiteral("flatpak"));
  command.program = flatpak.isEmpty() ? QStringLiteral("flatpak") : flatpak;
  const bool system_wide = to_qstring(install.flatpak_root).startsWith(QLatin1String("/var/lib/flatpak"));
  command.arguments = {QStringLiteral("run"),
                       system_wide ? QStringLiteral("--system") : QStringLiteral("--user"),
                       QStringLiteral("--die-with-parent"),
                       QStringLiteral("--filesystem=%1:ro").arg(QFileInfo(helper).absolutePath()),
                       QStringLiteral("--command=%1").arg(helper),
                       QString::fromStdString(install.flatpak_app),
                       QStringLiteral("--gegl"),
                       library};
  return command;
}

QString gegl_category_label(const QString& category) {
  // GEGL's categories are not in its catalog: Patchy names the ones GIMP 3.2's GEGL offers.
  static const std::map<QString, const char*> kNames = {
      {QStringLiteral("artistic"), QT_TRANSLATE_NOOP("GeglFilters", "Artistic")},
      {QStringLiteral("blur"), QT_TRANSLATE_NOOP("GeglFilters", "Blur")},
      {QStringLiteral("color"), QT_TRANSLATE_NOOP("GeglFilters", "Color")},
      {QStringLiteral("distort"), QT_TRANSLATE_NOOP("GeglFilters", "Distort")},
      {QStringLiteral("dither"), QT_TRANSLATE_NOOP("GeglFilters", "Dither")},
      {QStringLiteral("edge-detect"), QT_TRANSLATE_NOOP("GeglFilters", "Edge Detect")},
      {QStringLiteral("enhance"), QT_TRANSLATE_NOOP("GeglFilters", "Enhance")},
      {QStringLiteral("frequency"), QT_TRANSLATE_NOOP("GeglFilters", "Frequency")},
      {QStringLiteral("generic"), QT_TRANSLATE_NOOP("GeglFilters", "Generic")},
      {QStringLiteral("grayscale"), QT_TRANSLATE_NOOP("GeglFilters", "Grayscale")},
      {QStringLiteral("illusions"), QT_TRANSLATE_NOOP("GeglFilters", "Illusions")},
      {QStringLiteral("light"), QT_TRANSLATE_NOOP("GeglFilters", "Light")},
      {QStringLiteral("map"), QT_TRANSLATE_NOOP("GeglFilters", "Map")},
      {QStringLiteral("noise"), QT_TRANSLATE_NOOP("GeglFilters", "Noise")},
      {QStringLiteral("render"), QT_TRANSLATE_NOOP("GeglFilters", "Render")},
      {QStringLiteral("segmentation"), QT_TRANSLATE_NOOP("GeglFilters", "Segmentation")},
      {QStringLiteral("tile"), QT_TRANSLATE_NOOP("GeglFilters", "Tile")},
      {QStringLiteral("tonemapping"), QT_TRANSLATE_NOOP("GeglFilters", "Tone Mapping")},
  };
  if (const auto it = kNames.find(category); it != kNames.end()) {
    return QCoreApplication::translate("GeglFilters", it->second);
  }
  auto words = category.split(QLatin1Char('-'), Qt::SkipEmptyParts);
  for (auto& word : words) {
    word[0] = word[0].toUpper();
  }
  return words.join(QLatin1Char(' '));
}

bool gegl_operation_offered(const QJsonObject& operation, QString* why) {
  const auto say = [why](const char* reason) {
    if (why != nullptr) {
      *why = QString::fromLatin1(reason);
    }
    return false;
  };
  const auto name = operation.value(QStringLiteral("name")).toString();
  if (!name.startsWith(QLatin1String("gegl:"))) {
    return say("not a gegl: operation");
  }
  const auto categories = operation.value(QStringLiteral("categories")).toString().toLower().split(QLatin1Char(':'));
  // A one-input filter, or a render operation without an input (Plasma, Solid Noise), whose
  // result replaces the layer as GIMP's Render filters do.
  const bool has_input = operation.value(QStringLiteral("input")).toBool();
  if (!operation.value(QStringLiteral("output")).toBool() || operation.value(QStringLiteral("aux")).toBool() ||
      (!has_input && !categories.contains(QStringLiteral("render")))) {
    return say("not a one-input image filter or a render operation");
  }
  if (categories.join(QString()).isEmpty()) {
    return say("no category");
  }
  for (const auto& category : categories) {
    if (kExcludedCategories.contains(category)) {
      return say("category not offered");
    }
  }
  if (excluded_by_name(name)) {
    return say("excluded by name (docs/legal-constraints.md)");
  }
  for (const auto& p : operation.value(QStringLiteral("properties")).toArray()) {
    // Text settings are file paths, matrices, graph descriptions (gegl:gegl, which could
    // load files) or the words of gegl:text: nothing that works at its default.
    const auto kind = p.toObject().value(QStringLiteral("kind")).toString();
    if (kind == QLatin1String("other") || kind == QLatin1String("string")) {
      return say("a setting Patchy cannot hold (text, path, curve, buffer)");
    }
  }
  return true;
}

GeglFilterScanResult scan_gegl_filters(const GeglHelperCommand& command, const QString& language) {
  GeglFilterScanResult result;
  result.library = command.library;
  if (!QFileInfo::exists(gegl_helper_path())) {
    result.problem =
        QCoreApplication::translate("GeglFilters", "Patchy's GEGL helper is missing from its program folder.");
    return result;
  }
  // The cache: the helper's last list, reused while its key matches. Bump kCacheVersion
  // when the helper's list changes shape.
  constexpr int kCacheVersion = 1;
  const auto stamp = [](const QString& path) {
    const QFileInfo info(path);
    return QStringLiteral("%1|%2|%3").arg(path).arg(info.size()).arg(info.lastModified().toMSecsSinceEpoch());
  };
  const auto key = QStringLiteral("%1|%2|%3|%4")
                       .arg(kCacheVersion)
                       .arg(stamp(command.library_file.isEmpty() ? command.library : command.library_file),
                            stamp(gegl_helper_path()), language);
  const auto cache_path = QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
                              .filePath(QStringLiteral("gegl-filters/operations.json"));
  QJsonObject document;
  {
    QFile cached(cache_path);
    if (cached.open(QIODevice::ReadOnly)) {
      const auto saved = QJsonDocument::fromJson(cached.readAll()).object();
      if (saved.value(QStringLiteral("key")).toString() == key) {
        document = saved.value(QStringLiteral("list")).toObject();
      }
    }
  }
  if (document.isEmpty()) {
    QProcess helper;
    prepare_helper(helper, command, language);
    helper.start(command.program, command.arguments + QStringList{QStringLiteral("--gpl3"), QStringLiteral("--list")});
    helper.closeWriteChannel();
    if (!helper.waitForFinished(60000) || helper.exitCode() != 0) {
      helper.kill();
      //: %1 is what the helper reported.
      result.problem = QCoreApplication::translate("GeglFilters", "GIMP's GEGL could not be loaded. %1")
                           .arg(QString::fromUtf8(helper.readAllStandardError()).trimmed().left(400));
      return result;
    }
    document = QJsonDocument::fromJson(helper.readAllStandardOutput()).object();
    if (!document.value(QStringLiteral("operations")).toArray().isEmpty()) {
      QDir().mkpath(QFileInfo(cache_path).absolutePath());
      QSaveFile out(cache_path);
      if (out.open(QIODevice::WriteOnly)) {
        QJsonObject saved;
        saved.insert(QStringLiteral("key"), key);
        saved.insert(QStringLiteral("list"), document);
        out.write(QJsonDocument(saved).toJson(QJsonDocument::Compact));
        out.commit();
      }
    }
  }
  auto registry = std::make_shared<FilterRegistry>();
  const auto operations = document.value(QStringLiteral("operations")).toArray();
  result.operations = static_cast<int>(operations.size());
  for (const auto& entry : operations) {
    const auto operation = entry.toObject();
    if (!gegl_operation_offered(operation)) {
      continue;
    }
    const auto name = operation.value(QStringLiteral("name")).toString();
    auto spec = std::make_shared<GeglOperationSpec>();
    spec->operation = name.toStdString();
    FilterDefinition filter;
    filter.identifier = kGeglFilterIdPrefix + name.mid(5).toStdString();
    auto title = operation.value(QStringLiteral("title")).toString();
    filter.display_name = (title.isEmpty() ? name.mid(5) : title).toStdString();
    for (const auto& p : operation.value(QStringLiteral("properties")).toArray()) {
      GeglSetting setting;
      if (auto parameter = parameter_for(p.toObject(), setting); parameter.has_value()) {
        filter.catalog.parameters.push_back(std::move(*parameter));
        spec->settings.push_back(std::move(setting));
      }
    }
    // The registry's compatibility path: GEGL's own defaults.
    filter.apply = [command, spec](PixelBuffer& pixels) { run_operation(command, *spec, {}, pixels, nullptr); };
    filter.catalog.execute = [command, spec](const FilterRegistry&, const FilterInvocation& invocation,
                                             PixelBuffer& pixels, const FilterProgress* progress) {
      run_operation(command, *spec, invocation, pixels, progress);
    };
    const auto categories = operation.value(QStringLiteral("categories")).toString().split(QLatin1Char(':'));
    result.categories[filter.identifier] = categories.value(0).toLower();
    result.descriptions[filter.identifier] = operation.value(QStringLiteral("description")).toString();
    registry->register_filter(std::move(filter));
  }
  result.registry = std::move(registry);
  return result;
}

}  // namespace patchy::ui
