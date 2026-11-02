// GEGL filters from Patchy's own GEGL (docs/gegl-filters.md): the scan through
// patchy-gegl-host and the FilterDefinitions that run one operation through it.

#include "ui/gegl_filters.hpp"

#include "core/layer.hpp"
#include "core/pixel_buffer.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/qt_paths.hpp"
#include "ui/theme_qss.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUrl>
#include <QVBoxLayout>

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
      QStringLiteral("gegl:median-blur"),  // Surface Blur rule: histogram form unverifiable
      QStringLiteral("gegl:focus-blur"),   // Blur Gallery record: a per-pixel blur radius mask
      // Not useful on a layer: no change to pixels in 0..1 (Absolute), damage to
      // straight-alpha pixels (Unpremultiply), nothing beyond the layer's own bounds (Tile),
      // a fixed-size solid rectangle (Rectangle; the shape tools do that), a drop shadow whose
      // offset and grow need operations Patchy's GEGL leaves out (Median Blur, Translate;
      // the Drop Shadow layer style does it better).
      QStringLiteral("gegl:dropshadow"),
      QStringLiteral("gegl:absolute"),
      QStringLiteral("gegl:unpremultiply"),
      QStringLiteral("gegl:tile"),
      QStringLiteral("gegl:rectangle"),
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

// The helper's environment. Off Windows GEGL and babl are pointed at their module folders
// (GEGL_PATH and BABL_PATH, their documented module paths) and, on Linux, the loader at the
// bundle's library folder (LD_LIBRARY_PATH; the build-tree bundle has no RUNPATHs). macOS
// libraries find each other through their @rpath names, which a hardened-runtime helper
// needs: it ignores DYLD_LIBRARY_PATH. Windows GEGL finds its modules relative to its DLL.
// LANGUAGE: GEGL translates its own labels through gettext (the helper asks its catalog).
void prepare_helper(QProcess& helper, const GeglHelperCommand& command, const QString& language = {}) {
  auto environment = QProcessEnvironment::systemEnvironment();
#ifndef Q_OS_WIN
  const auto library_folder = QFileInfo(command.library).absolutePath();
#ifndef Q_OS_MACOS
  const auto previous = environment.value(QStringLiteral("LD_LIBRARY_PATH"));
  environment.insert(QStringLiteral("LD_LIBRARY_PATH"),
                     previous.isEmpty() ? library_folder : library_folder + QLatin1Char(':') + previous);
#endif
  // The macOS app bundle renames the module folders and keeps the catalogs in Resources
  // (CMakeLists.txt: codesign refuses dotted folder names in Frameworks); build trees and
  // Linux keep GEGL's own names and share/locale beside lib.
  const QDir lib(library_folder);
  const auto modules = lib.exists(QStringLiteral("gegl-modules")) ? QStringLiteral("gegl-modules") : QStringLiteral("gegl-0.4");
  const auto extensions =
      lib.exists(QStringLiteral("babl-extensions")) ? QStringLiteral("babl-extensions") : QStringLiteral("babl-0.1");
  environment.insert(QStringLiteral("GEGL_PATH"), lib.filePath(modules));
  environment.insert(QStringLiteral("BABL_PATH"), lib.filePath(extensions));
  for (const auto& locale : {lib.filePath(QStringLiteral("../share/locale")),
                             lib.filePath(QStringLiteral("../../../Resources/gegl/share/locale"))}) {
    if (QFileInfo(locale).isDir()) {
      environment.insert(QStringLiteral("PATCHY_GEGL_LOCALE_DIR"), QDir::cleanPath(locale));
      break;
    }
  }
#else
  (void)command;
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
      double ui_low = p.value(QStringLiteral("uiMin")).toDouble();
      double ui_high = p.value(QStringLiteral("uiMax")).toDouble();
      const double fallback = p.value(QStringLiteral("default")).toDouble();
      const bool pixels = d.spatial_scale == FilterSpatialScale::Pixels;
      // GEGL fills a missing UI range with the value range ("Settings" in
      // docs/gegl-filters.md): an unbounded one (Convolution Matrix, gradient points)
      // would make a slider of plus or minus a million, and an integer's whole range
      // (Dither's 2 to 65536 levels) one where the useful values are a pixel wide. The
      // spin box still takes any value inside the limits.
      if (ui_low <= -kLargestValue || ui_high >= kLargestValue) {
        if (pixels) {
          ui_low = fallback >= 0.0 ? 0.0 : -4096.0;
          ui_high = 4096.0;
        } else {
          const double extent = std::max(10.0, 2.0 * std::abs(fallback));
          ui_low = -extent;
          ui_high = extent;
        }
      } else if (integer && !pixels && p.value(QStringLiteral("role")).toString().isEmpty() &&
                 ui_low == p.value(QStringLiteral("min")).toDouble() &&
                 ui_high == p.value(QStringLiteral("max")).toDouble() && ui_high - ui_low > 1024.0) {
        ui_high = std::max(ui_low + 1.0, std::max(256.0, 4.0 * fallback));
      }
      const double ui_minimum = std::clamp(ui_low, *d.minimum, *d.maximum);
      const double ui_maximum = std::clamp(ui_high, ui_minimum, *d.maximum);
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
  if (QFileInfo::exists(QStringLiteral("/.flatpak-info"))) {
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
  command.program = gegl_helper_path();
  command.arguments = {QStringLiteral("--gegl"), library};
  return command;
}

QString gegl_category_label(const QString& category) {
  // GEGL's categories are not in its catalog: Patchy names the ones GEGL 0.4 offers.
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
  // result replaces the layer inside the selection.
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
  constexpr int kCacheVersion = 2;  // 2: GEGL's version
  const auto stamp = [](const QString& path) {
    const QFileInfo info(path);
    return QStringLiteral("%1|%2|%3").arg(path).arg(info.size()).arg(info.lastModified().toMSecsSinceEpoch());
  };
  const auto key = QStringLiteral("%1|%2|%3|%4")
                       .arg(kCacheVersion)
                       .arg(stamp(command.library),
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
      result.problem = QCoreApplication::translate("GeglFilters", "Patchy's GEGL could not be loaded. %1")
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
  result.version = document.value(QStringLiteral("version")).toString();
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
    result.licenses[filter.identifier] = operation.value(QStringLiteral("license")).toString();
    registry->register_filter(std::move(filter));
  }
  result.registry = std::move(registry);
  return result;
}

QString gegl_filter_credit(const GeglFilterScanResult& scan, const std::string& identifier) {
  const auto found = scan.licenses.find(identifier);
  const bool gpl3 = found != scan.licenses.end() && found->second.startsWith(QLatin1String("GPL3"));
  const auto version = scan.version.isEmpty() ? QString() : QLatin1Char(' ') + scan.version;
  //: %1 is GEGL's version with a leading space (" 0.4.72"), or nothing when it is not known.
  return gpl3 ? QCoreApplication::translate("GeglFilters", "Filter from GEGL%1 (gegl.org), GPL-3").arg(version)
              //: %1 is GEGL's version with a leading space (" 0.4.72"), or nothing when it is not known.
              : QCoreApplication::translate("GeglFilters", "Filter from GEGL%1 (gegl.org)").arg(version);
}

QString gegl_licenses_folder() {
  // Where each package puts them (docs/gegl-filters.md, "Bundled GEGL"): beside the program
  // on Windows and in build trees, the app's Resources on macOS, the installed bundle on
  // Linux, the Flatpak's license folder.
  const QDir program(QCoreApplication::applicationDirPath());
  for (const auto& candidate :
       {QStringLiteral("gegl/licenses"), QStringLiteral("../Resources/gegl/licenses"),
        QStringLiteral("../lib/patchy/gegl/licenses"), QStringLiteral("../share/licenses/com.rtsoft.patchy")}) {
    if (const QFileInfo info(program.filePath(candidate)); info.isDir()) {
      return info.canonicalFilePath();
    }
  }
  return {};
}

void show_about_gegl(QWidget* parent, const GeglFilterScanResult* scan) {
  QDialog about(parent);
  about.setObjectName(QStringLiteral("geglAboutDialog"));
  about.setWindowTitle(QCoreApplication::translate("GeglFilters", "About GEGL"));
  auto* layout = new QVBoxLayout(&about);
  const auto version = scan != nullptr ? scan->version : QString();
  auto* heading = new QLabel(version.isEmpty() ? QStringLiteral("GEGL") : QStringLiteral("GEGL %1").arg(version), &about);
  heading->setObjectName(QStringLiteral("geglAboutHeading"));
  heading->setFont(offset_font(heading->font(), 3, true));
  layout->addWidget(heading);
  const int offered =
      scan != nullptr && scan->registry != nullptr ? static_cast<int>(scan->registry->filters().size()) : 0;
  const QStringList paragraphs{
      QCoreApplication::translate("GeglFilters",
                                  "GEGL (Generic Graphics Library) is a free, open-source image processing engine, "
                                  "with babl handling its pixel formats. It is the engine behind GIMP's filters."),
      QCoreApplication::translate("GeglFilters",
                                  "Patchy includes its own build of GEGL and offers %n of its operations under "
                                  "Filter > GEGL Filters, run in Patchy's own dialogs. GIMP is not needed.",
                                  nullptr, offered),
      QCoreApplication::translate("GeglFilters",
                                  "GEGL and babl are made by Øyvind Kolås and the GEGL and GIMP contributors."),
      QCoreApplication::translate("GeglFilters",
                                  "GEGL and babl are distributed under the GNU LGPL version 3 or later. The filters "
                                  "marked GPL-3 come from GEGL's GPL-3 module and are under the GNU GPL version 3 or "
                                  "later. Their license texts are included with Patchy, and the complete source of "
                                  "Patchy's GEGL is PatchyGeglSources.zip, published with every Patchy release."),
      QCoreApplication::translate("GeglFilters",
                                  "Questions about how Patchy runs a filter belong with Patchy, not the GEGL team."),
  };
  auto* body = new QLabel(&about);
  body->setObjectName(QStringLiteral("geglAboutText"));
  body->setTextFormat(Qt::RichText);
  body->setWordWrap(true);
  QString html;
  for (const auto& paragraph : paragraphs) {
    html += QStringLiteral("<p>%1</p>").arg(paragraph.toHtmlEscaped());
  }
  body->setText(html);
  body->setMinimumWidth(420);
  layout->addWidget(body);
  const auto add_link = [&](const char* object_name, const QString& href, const QString& text) {
    auto* link = new QLabel(&about);
    link->setObjectName(QLatin1String(object_name));
    link->setTextFormat(Qt::RichText);
    link->setTextInteractionFlags(Qt::TextBrowserInteraction);
    link->setOpenExternalLinks(true);
    set_themed_label_text(*link, QStringLiteral("<a style=\"color:@accent\" href=\"%1\">%2</a>")
                                     .arg(href.toHtmlEscaped(), text.toHtmlEscaped()));
    layout->addWidget(link);
  };
  add_link("geglAboutLink", QStringLiteral("https://gegl.org"),
           QCoreApplication::translate("GeglFilters", "Visit the GEGL website (gegl.org)"));
  if (const auto licenses = gegl_licenses_folder(); !licenses.isEmpty()) {
    add_link("geglAboutLicensesLink", QUrl::fromLocalFile(licenses).toString(),
             QCoreApplication::translate("GeglFilters", "Show the license texts"));
  }
  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok, &about);
  QObject::connect(buttons, &QDialogButtonBox::accepted, &about, &QDialog::accept);
  layout->addWidget(buttons);
  exec_dialog(about);
}

}  // namespace patchy::ui
