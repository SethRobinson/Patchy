// GEGL filters from the user's GIMP 3 (docs/gegl-filters.md): the scan through
// patchy-gegl-host and the FilterDefinitions that run one operation through it.

#include "ui/gegl_filters.hpp"

#include "core/pixel_buffer.hpp"
#include "ui/qt_paths.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
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

// Left out by name, near the boundaries in docs/legal-constraints.md (healing,
// inpainting, red eye), whatever their category.
bool excluded_by_name(const QString& name) {
  return name.contains(QLatin1String("inpaint")) || name.contains(QLatin1String("seamless-clone")) ||
         name.contains(QLatin1String("red-eye")) || name.contains(QLatin1String("heal"));
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

// PATCHY_GEGL_TRACE=1: each run's steps on stderr.
void trace(const char* step, const GeglOperationSpec& spec, qsizetype bytes) {
  static const bool enabled = qEnvironmentVariableIntValue("PATCHY_GEGL_TRACE") != 0;
  if (enabled) {
    std::fprintf(stderr, "gegl: %s %s (%lld bytes)\n", step, spec.operation.c_str(), static_cast<long long>(bytes));
    std::fflush(stderr);
  }
}

void run_operation(const QString& library, const GeglOperationSpec& spec, const FilterInvocation& invocation,
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
    if (setting.kind == 'e') {
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
  helper.setProgram(gegl_helper_path());
  helper.setArguments({QStringLiteral("--gegl"), library, QStringLiteral("--gpl3"), QStringLiteral("--run")});
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

QString find_gegl_library(const gimp::GimpInstall& install) {
  if (install.flatpak) {
    return {};
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

QString gegl_category_label(const QString& category) {
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
  if (!operation.value(QStringLiteral("input")).toBool() || !operation.value(QStringLiteral("output")).toBool() ||
      operation.value(QStringLiteral("aux")).toBool()) {
    return say("not a one-input image filter");
  }
  const auto categories = operation.value(QStringLiteral("categories")).toString().toLower().split(QLatin1Char(':'));
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
    if (p.toObject().value(QStringLiteral("kind")).toString() == QLatin1String("other")) {
      return say("a setting Patchy cannot hold (path, curve, buffer)");
    }
  }
  return true;
}

GeglFilterScanResult scan_gegl_filters(const QString& library) {
  GeglFilterScanResult result;
  result.library = library;
  const auto helper_path = gegl_helper_path();
  if (!QFileInfo::exists(helper_path)) {
    result.problem =
        QCoreApplication::translate("GeglFilters", "Patchy's GEGL helper is missing from its program folder.");
    return result;
  }
  QProcess helper;
  helper.start(helper_path, {QStringLiteral("--gegl"), library, QStringLiteral("--gpl3"), QStringLiteral("--list")});
  helper.closeWriteChannel();
  if (!helper.waitForFinished(60000) || helper.exitCode() != 0) {
    helper.kill();
    //: %1 is what the helper reported.
    result.problem = QCoreApplication::translate("GeglFilters", "GIMP's GEGL could not be loaded. %1")
                         .arg(QString::fromUtf8(helper.readAllStandardError()).trimmed().left(400));
    return result;
  }
  const auto document = QJsonDocument::fromJson(helper.readAllStandardOutput()).object();
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
    filter.apply = [library, spec](PixelBuffer& pixels) { run_operation(library, *spec, {}, pixels, nullptr); };
    filter.catalog.execute = [library, spec](const FilterRegistry&, const FilterInvocation& invocation,
                                             PixelBuffer& pixels, const FilterProgress* progress) {
      run_operation(library, *spec, invocation, pixels, progress);
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
