#include "ui/theme_file.hpp"

#include "patchy_version.hpp"
#include "ui/theme_package.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QIODevice>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QVersionNumber>

#include <utility>

// The bundled theme resources (themes.qrc and the generated theme_packages.qrc)
// live in the static patchy_ui library; force registration before first use, as
// icon_theme.cpp does for icons.qrc.
int qInitResources_themes();
int qInitResources_theme_packages();

namespace patchy::ui {

namespace {

void ensure_theme_resources() {
  static const int registered = ::qInitResources_themes() + ::qInitResources_theme_packages();
  (void)registered;
}

// A bundled package's folder in the source tree, when PATCHY_THEME_SOURCE_DIR names the
// themes/bundled folder that holds it; empty otherwise.
QString bundled_package_source_dir(const QString& name) {
  const auto source = qEnvironmentVariable("PATCHY_THEME_SOURCE_DIR");
  if (source.isEmpty() || name.isEmpty() || name.contains(QLatin1Char('/')) || name.contains(QLatin1Char('\\')) ||
      name.startsWith(QLatin1Char('.'))) {
    return {};
  }
  const QDir folder(QDir(source).filePath(name));
  return QFileInfo(folder.filePath(QString(kThemePackageManifest))).isFile() ? folder.absolutePath() : QString();
}

[[nodiscard]] std::optional<ColorScheme> parse_base_token(const QString& token) {
  if (token == QStringLiteral("dark")) {
    return ColorScheme::Dark;
  }
  if (token == QStringLiteral("light")) {
    return ColorScheme::Light;
  }
  return std::nullopt;
}

[[nodiscard]] QString base_token(ColorScheme scheme) {
  return scheme == ColorScheme::Light ? QStringLiteral("light") : QStringLiteral("dark");
}

[[nodiscard]] bool is_hex_digit(QChar c) {
  return (c >= QLatin1Char('0') && c <= QLatin1Char('9')) || (c >= QLatin1Char('a') && c <= QLatin1Char('f')) ||
         (c >= QLatin1Char('A') && c <= QLatin1Char('F'));
}

[[nodiscard]] QString color_to_hex(QColor color) {
  const auto byte = [](int value) { return QStringLiteral("%1").arg(value, 2, 16, QLatin1Char('0')); };
  auto text = QLatin1Char('#') + byte(color.red()) + byte(color.green()) + byte(color.blue());
  if (color.alpha() != 255) {
    text += byte(color.alpha());
  }
  return text;
}

// Mirrors role_members() in theme_qss.cpp: a name-to-member lookup built once
// from the same authoritative table.
const QHash<QString, QColor ThemePalette::*>& role_members() {
  static const auto members = [] {
    QHash<QString, QColor ThemePalette::*> table;
    for (const auto& [name, member] : theme_palette_roles()) {
      table.insert(QString(name), member);
    }
    return table;
  }();
  return members;
}

// The package keys (docs/theme-files.md, "Theme packages"). A bad value is a
// warning, never an error: the colors still load and the key keeps its default.
void parse_package_options(const QJsonObject& object, ThemePackageOptions& options, QStringList& warnings) {
  const auto recolor = object.value(QStringLiteral("recolorIcons"));
  if (recolor.isBool()) {
    options.recolor_icons = recolor.toBool();
  } else if (!recolor.isUndefined()) {
    warnings.push_back(QCoreApplication::translate("ThemeFile", "\"recolorIcons\" must be true or false (ignored)."));
  }
  const auto scaling = object.value(QStringLiteral("iconScaling"));
  if (scaling.toString() == QStringLiteral("smooth")) {
    options.icon_scaling = ThemeIconScaling::Smooth;
  } else if (scaling.toString() == QStringLiteral("nearest")) {
    options.icon_scaling = ThemeIconScaling::Nearest;
  } else if (!scaling.isUndefined()) {
    warnings.push_back(
        QCoreApplication::translate("ThemeFile", "\"iconScaling\" must be \"smooth\" or \"nearest\" (ignored)."));
  }
  const auto backdrop = object.value(QStringLiteral("backdrop"));
  if (backdrop.isUndefined()) {
    return;
  }
  if (!backdrop.isObject()) {
    warnings.push_back(QCoreApplication::translate("ThemeFile", "\"backdrop\" is not an object (ignored)."));
    return;
  }
  const auto backdrop_object = backdrop.toObject();
  options.backdrop_image = backdrop_object.value(QStringLiteral("image")).toString();
  const auto mode = backdrop_object.value(QStringLiteral("mode")).toString(QStringLiteral("tile"));
  if (mode == QStringLiteral("tile")) {
    options.backdrop_mode = ThemeBackdropMode::Tile;
  } else if (mode == QStringLiteral("stretch")) {
    options.backdrop_mode = ThemeBackdropMode::Stretch;
  } else if (mode == QStringLiteral("center")) {
    options.backdrop_mode = ThemeBackdropMode::Center;
  } else {
    warnings.push_back(QCoreApplication::translate(
        "ThemeFile", "Backdrop mode \"%1\" is not \"tile\", \"stretch\" or \"center\" (tiling instead).")
                           .arg(mode));
  }
}

[[nodiscard]] QString backdrop_mode_token(ThemeBackdropMode mode) {
  switch (mode) {
    case ThemeBackdropMode::Stretch:
      return QStringLiteral("stretch");
    case ThemeBackdropMode::Center:
      return QStringLiteral("center");
    case ThemeBackdropMode::Tile:
      break;
  }
  return QStringLiteral("tile");
}

// "version", "author", "minPatchyVersion" and "description": text, trimmed and capped so
// a list row stays readable; a bad value is a warning and the key stays empty. Only the
// description keeps its line breaks (the details panel wraps it).
void parse_theme_info(const QJsonObject& object, ThemeInfo& info, QStringList& warnings) {
  const auto text = [&object, &warnings](const QString& key, qsizetype limit, bool multiline = false) {
    const auto value = object.value(key);
    if (value.isUndefined()) {
      return QString();
    }
    if (!value.isString()) {
      warnings.push_back(QCoreApplication::translate("ThemeFile", "\"%1\" must be text (ignored).").arg(key));
      return QString();
    }
    if (multiline) {
      auto lines = value.toString().replace(QStringLiteral("\r\n"), QStringLiteral("\n")).split(QLatin1Char('\n'));
      for (auto& line : lines) {
        line = line.simplified();
      }
      return lines.join(QLatin1Char('\n')).trimmed().left(limit).trimmed();
    }
    return value.toString().simplified().left(limit);
  };
  info.version = text(QStringLiteral("version"), 32);
  info.author = text(QStringLiteral("author"), 100);
  info.description = text(QStringLiteral("description"), kThemeDescriptionMaxLength, true);
  const auto minimum = text(QStringLiteral("minPatchyVersion"), 32);
  static const QRegularExpression dotted(QStringLiteral("^\\d{1,4}(\\.\\d{1,4}){0,3}$"));
  if (dotted.match(minimum).hasMatch()) {
    info.min_patchy_version = minimum;
  } else if (!minimum.isEmpty()) {
    warnings.push_back(QCoreApplication::translate(
                           "ThemeFile", "\"minPatchyVersion\" must be a version such as \"1.20\" (ignored)."));
  }
}

}  // namespace

std::optional<QColor> parse_theme_color(const QString& text) {
  if ((text.size() != 7 && text.size() != 9) || text.isEmpty() || text[0] != QLatin1Char('#')) {
    return std::nullopt;
  }
  for (int i = 1; i < text.size(); ++i) {
    if (!is_hex_digit(text[i])) {
      return std::nullopt;
    }
  }
  bool ok = false;
  const auto rgb = text.mid(1, 6).toUInt(&ok, 16);
  if (!ok) {
    return std::nullopt;
  }
  auto color = QColor::fromRgb(rgb);
  if (text.size() == 9) {
    const auto alpha = text.mid(7, 2).toUInt(&ok, 16);
    if (!ok) {
      return std::nullopt;
    }
    color.setAlpha(static_cast<int>(alpha));
  }
  return color;
}

ThemeLoadResult load_theme_from_json(const QByteArray& json) {
  ThemeLoadResult result;

  QJsonParseError parse_error;
  const auto document = QJsonDocument::fromJson(json, &parse_error);
  if (parse_error.error != QJsonParseError::NoError || !document.isObject()) {
    result.error = QCoreApplication::translate("ThemeFile", "Not a valid theme file: %1")
                       .arg(parse_error.error != QJsonParseError::NoError
                                ? parse_error.errorString()
                                : QCoreApplication::translate("ThemeFile", "the top level is not an object"));
    return result;
  }

  const auto object = document.object();
  // "format" is optional (an absent one is format 1, the shape this build
  // writes); any other value means a newer build wrote keys this one cannot
  // interpret, which is a hard error rather than a silent partial load.
  const auto format_value = object.value(QStringLiteral("format"));
  if (!format_value.isUndefined() && (!format_value.isDouble() || format_value.toInt(-1) != kThemeFileFormat)) {
    result.error = QCoreApplication::translate("ThemeFile",
                                               "Theme file format %1 is not supported by this build (expected %2).")
                       .arg(format_value.toVariant().toString())
                       .arg(kThemeFileFormat);
    return result;
  }
  const auto base = parse_base_token(object.value(QStringLiteral("base")).toString());
  if (!base) {
    result.error =
        QCoreApplication::translate("ThemeFile", "Theme file is missing a valid \"base\" (must be \"dark\" or \"light\").");
    return result;
  }

  // "roles" itself is optional (an absent one means "no overrides, just the
  // base scheme"); when present it must be an object, and its individual
  // entries are validated below.
  const auto roles_value = object.value(QStringLiteral("roles"));
  if (!roles_value.isUndefined() && !roles_value.isObject()) {
    result.error = QCoreApplication::translate("ThemeFile", "Theme file's \"roles\" is not an object.");
    return result;
  }

  CustomTheme custom_theme;
  custom_theme.base = *base;
  custom_theme.name = object.value(QStringLiteral("name")).toString();
  custom_theme.palette = theme(*base);  // seed: every role the file omits keeps this value.
  parse_package_options(object, custom_theme.options, result.warnings);
  parse_theme_info(object, custom_theme.info, result.warnings);

  const auto& members = role_members();
  const auto roles_object = roles_value.toObject();
  for (auto it = roles_object.constBegin(); it != roles_object.constEnd(); ++it) {
    const auto member = members.find(it.key());
    if (member == members.end()) {
      result.warnings.push_back(
          QCoreApplication::translate("ThemeFile", "Unknown color role \"%1\" (ignored).").arg(it.key()));
      continue;
    }
    const auto color = parse_theme_color(it.value().toString());
    if (!color) {
      result.error = QCoreApplication::translate(
                         "ThemeFile", "Invalid color \"%1\" for role \"%2\" (expected #RRGGBB or #RRGGBBAA).")
                         .arg(it.value().toString(), it.key());
      result.warnings.clear();
      return result;
    }
    custom_theme.palette.*(member.value()) = *color;
  }

  result.theme = std::move(custom_theme);
  return result;
}

QByteArray serialize_theme_to_json(const ThemePalette& palette, ColorScheme base, const QString& name,
                                   const ThemePackageOptions* options, const ThemeInfo* info) {
  QJsonObject roles_object;
  for (const auto& [role_name, member] : theme_palette_roles()) {
    roles_object.insert(QString(role_name), color_to_hex(palette.*member));
  }

  QJsonObject object;
  object.insert(QStringLiteral("format"), kThemeFileFormat);
  object.insert(QStringLiteral("name"), name);
  if (info != nullptr) {
    for (const auto& [key, value] : {std::pair{QStringLiteral("version"), info->version},
                                     std::pair{QStringLiteral("author"), info->author},
                                     std::pair{QStringLiteral("minPatchyVersion"), info->min_patchy_version},
                                     std::pair{QStringLiteral("description"), info->description}}) {
      if (!value.isEmpty()) {
        object.insert(key, value);
      }
    }
  }
  object.insert(QStringLiteral("base"), base_token(base));
  object.insert(QStringLiteral("roles"), roles_object);
  if (options != nullptr) {
    if (!options->recolor_icons) {
      object.insert(QStringLiteral("recolorIcons"), false);
    }
    if (options->icon_scaling == ThemeIconScaling::Nearest) {
      object.insert(QStringLiteral("iconScaling"), QStringLiteral("nearest"));
    }
    if (!options->backdrop_image.isEmpty()) {
      QJsonObject backdrop;
      backdrop.insert(QStringLiteral("image"), options->backdrop_image);
      backdrop.insert(QStringLiteral("mode"), backdrop_mode_token(options->backdrop_mode));
      object.insert(QStringLiteral("backdrop"), backdrop);
    }
  }
  return QJsonDocument(object).toJson(QJsonDocument::Indented);
}

QString running_patchy_version() { return QStringLiteral(PATCHY_VERSION); }

bool theme_needs_newer_patchy(const ThemeInfo& info) {
  if (info.min_patchy_version.isEmpty()) {
    return false;
  }
  return QVersionNumber::fromString(info.min_patchy_version) >
         QVersionNumber::fromString(running_patchy_version());
}

bool is_bundled_theme_id(const QString& id) { return id.startsWith(kBundledThemeIdPrefix); }

QStringList bundled_theme_file_names() {
  // Listed in an authored order rather than alphabetically: the two neutral
  // brightness steps first, then the named palettes, then the accessibility one, then
  // the packages (folders, theme_packages.qrc).
  static const QStringList names{
      QStringLiteral("darkest.patchytheme"),      QStringLiteral("medium-gray.patchytheme"),
      QStringLiteral("solarized-dark.patchytheme"), QStringLiteral("nord.patchytheme"),
      QStringLiteral("dracula.patchytheme"),      QStringLiteral("gruvbox-dark.patchytheme"),
      QStringLiteral("high-contrast.patchytheme"), QStringLiteral("dink-smallwood"),
  };
  return names;
}

QString theme_file_path_for_id(const QString& id) {
  if (is_bundled_theme_id(id)) {
    const auto name = id.mid(kBundledThemeIdPrefix.size());
    if (const auto source = bundled_package_source_dir(name); !source.isEmpty()) {
      return source;
    }
    ensure_theme_resources();
    return QStringLiteral(":/patchy/themes/") + name;
  }
  const auto directory = user_themes_directory();
  return directory.isEmpty() ? QString() : QDir(directory).filePath(id);
}

namespace {

ThemeLoadResult load_theme_for_id(const QString& id, bool summary) {
  const auto path = theme_file_path_for_id(id);
  if (!path.isEmpty()) {
    // A bundled package is a resource folder, or its source folder (PATCHY_THEME_SOURCE_DIR).
    if (QFileInfo(path).isDir()) {
      return summary ? load_theme_package_directory_summary(path) : load_theme_package_directory(path);
    }
    if (!is_bundled_theme_id(id) && is_theme_package_zip_name(id)) {
      if (!summary) {
        return load_theme_package_zip_file(path);
      }
      QFile zip(path);
      if (zip.open(QIODevice::ReadOnly) && zip.size() <= ThemePackageLimits{}.max_package_bytes) {
        return load_theme_package_zip_summary(zip.readAll());
      }
      // Too large or unreadable: the full loader says which.
      return load_theme_package_zip_file(path);
    }
  }
  QFile file(path);
  if (path.isEmpty() || !file.open(QIODevice::ReadOnly)) {
    ThemeLoadResult result;
    result.error = QCoreApplication::translate("ThemeFile", "Theme file \"%1\" could not be read.").arg(id);
    return result;
  }
  return load_theme_from_json(file.readAll());
}

}  // namespace

ThemeLoadResult load_theme_by_id(const QString& id) { return load_theme_for_id(id, /*summary=*/false); }

ThemeLoadResult load_theme_summary_by_id(const QString& id) { return load_theme_for_id(id, /*summary=*/true); }

bool is_theme_package_directory_id(const QString& id) {
  if (id.isEmpty()) {
    return false;
  }
  if (is_bundled_theme_id(id)) {
    return !bundled_package_source_dir(id.mid(kBundledThemeIdPrefix.size())).isEmpty();
  }
  const auto path = theme_file_path_for_id(id);
  return !path.isEmpty() && QFileInfo(path).isDir();
}

QStringList user_theme_ids() {
  const auto directory = user_themes_directory();
  if (directory.isEmpty()) {
    return {};
  }
  const QDir dir(directory);
  QStringList ids = dir.entryList({QStringLiteral("*.patchytheme"), QStringLiteral("*.zip")}, QDir::Files, QDir::Name);
  for (const auto& folder : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
    if (QFileInfo(QDir(dir.filePath(folder)).filePath(QString(kThemePackageManifest))).isFile()) {
      ids.push_back(folder);
    }
  }
  ids.sort(Qt::CaseInsensitive);
  return ids;
}

QString user_themes_directory() {
#ifdef Q_OS_WASM
  return {};
#else
  const auto override_dir = qEnvironmentVariable("PATCHY_THEMES_DIR");
  if (!override_dir.isEmpty()) {
    return QDir(override_dir).absolutePath();
  }
  const auto base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  if (base.isEmpty()) {
    return {};
  }
  return base + QStringLiteral("/themes");
#endif
}

}  // namespace patchy::ui
