#include "ui/theme_package.hpp"

#include "formats/ora_document_io.hpp"
#include "formats/zip_entry_reader.hpp"
#include "ui/icon_theme.hpp"
#include "ui/sound_effects.hpp"
#include "ui/theme_sounds.hpp"

#include <QBuffer>
#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QSaveFile>
#include <QRegularExpression>
#include <QSet>
#include <QXmlStreamReader>

#include <algorithm>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

namespace patchy::ui {

namespace {

std::shared_ptr<const ThemeAssets>& active_assets_slot() {
  static std::shared_ptr<const ThemeAssets> assets;
  return assets;
}

[[nodiscard]] QString tr_theme(const char* text) { return QCoreApplication::translate("ThemeFile", text); }

[[nodiscard]] QString lower_suffix(const QString& file_name) {
  const auto dot = file_name.lastIndexOf(QLatin1Char('.'));
  return dot < 0 ? QString() : file_name.mid(dot + 1).toLower();
}

// Decodes a PNG, BMP or JPEG named by its extension (never by content, so an
// image plug-in for another format cannot be reached through a renamed file),
// refusing anything past `max_side` before decoding it.
[[nodiscard]] QImage decode_package_image(const QByteArray& bytes, const QString& suffix, int max_side) {
  QByteArray format;
  if (suffix == QStringLiteral("png")) {
    format = "png";
  } else if (suffix == QStringLiteral("bmp")) {
    format = "bmp";
  } else if (suffix == QStringLiteral("jpg") || suffix == QStringLiteral("jpeg")) {
    format = "jpeg";
  } else {
    return {};
  }
  QBuffer buffer;
  buffer.setData(bytes);
  if (!buffer.open(QIODevice::ReadOnly)) {
    return {};
  }
  QImageReader reader(&buffer, format);
  reader.setDecideFormatFromContent(false);
  const auto size = reader.size();
  if (!size.isValid() || size.width() > max_side || size.height() > max_side) {
    return {};
  }
  const auto image = reader.read();
  return image.isNull() ? QImage() : image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
}

[[nodiscard]] std::vector<std::uint8_t> to_bytes(const QByteArray& bytes) {
  const auto* data = reinterpret_cast<const std::uint8_t*>(bytes.constData());
  return {data, data + bytes.size()};
}

[[nodiscard]] bool is_wav(const QByteArray& bytes) { return is_well_formed_wav(bytes); }

[[nodiscard]] bool is_known_effect(const QString& name) {
  return name == QStringLiteral("fireworks") || name == QStringLiteral("sparkles") ||
         name == QStringLiteral("confetti") || name == QStringLiteral("sprite");
}

// theme.json "events": {"<event id>": {"sound": "sounds/x.wav", "effect": "fireworks" | {...}}}.
void parse_event_actions(ThemeAssets& assets, const QHash<QString, QByteArray>& files, QStringList& warnings) {
  const auto events = assets.manifest.value(QStringLiteral("events")).toObject();
  for (auto it = events.begin(); it != events.end(); ++it) {
    if (!theme_sound_event_from_id(it.key())) {
      warnings.push_back(
          QCoreApplication::translate("ThemeFile", "Event \"%1\" is not a theme event (ignored).").arg(it.key()));
      continue;
    }
    const auto entry = it.value().toObject();
    ThemeEventAction action;
    if (const auto sound = entry.value(QStringLiteral("sound")).toString(); !sound.isEmpty()) {
      const auto bytes = is_safe_theme_package_path(sound) ? files.value(sound) : QByteArray();
      if (!is_wav(bytes)) {
        warnings.push_back(QCoreApplication::translate("ThemeFile", "Sound \"%1\" is not a WAV file in the package (ignored).")
                               .arg(sound));
      } else {
        action.sound = bytes;
      }
    }
    const auto effect = entry.value(QStringLiteral("effect"));
    QVariantMap spec = effect.isObject() ? effect.toObject().toVariantMap() : QVariantMap{};
    if (effect.isString()) {
      spec.insert(QStringLiteral("name"), effect.toString());
    }
    if (!spec.isEmpty()) {
      const auto name = spec.value(QStringLiteral("name")).toString();
      const auto image = spec.value(QStringLiteral("image")).toString();
      if (!is_known_effect(name)) {
        warnings.push_back(
            QCoreApplication::translate("ThemeFile", "Effect \"%1\" is not a theme effect (ignored).").arg(name));
      } else if (name == QStringLiteral("sprite") &&
                 (!is_safe_theme_package_path(image) || !files.contains(image))) {
        warnings.push_back(QCoreApplication::translate("ThemeFile", "Sprite image \"%1\" is not in the package (ignored).")
                               .arg(image));
      } else {
        action.effect = spec;
      }
    }
    if (!action.sound.isEmpty() || !action.effect.isEmpty()) {
      assets.events.insert(it.key(), action);
    }
  }
}

// Turns a package's files (relative path to bytes, theme.json included) into a
// theme: the manifest's colors and keys, then its icons, sounds and backdrop.
ThemeLoadResult load_theme_package_files(QHash<QString, QByteArray> files, const ThemePackageLimits& limits) {
  const auto manifest = files.take(QString(kThemePackageManifest));
  if (manifest.isEmpty()) {
    ThemeLoadResult missing;
    missing.error = tr_theme(QT_TRANSLATE_NOOP("ThemeFile", "The theme package has no theme.json."));
    return missing;
  }
  auto result = load_theme_from_json(manifest);
  if (!result.theme) {
    return result;
  }
  auto assets = std::make_shared<ThemeAssets>();
  assets->options = result.theme->options;
  assets->info = result.theme->info;

  QSet<QString> stems;
  for (const auto& stem : overridable_icon_stems()) {
    stems.insert(stem);
  }
  auto paths = files.keys();
  paths.sort();
  for (const auto& path : paths) {
    const auto& bytes = files[path];
    if (path.startsWith(QStringLiteral("icons/"))) {
      const auto file_name = path.mid(6);
      if (file_name.contains(QLatin1Char('/'))) {
        continue;  // icons/ is flat; a subfolder is the author's scratch space
      }
      const auto suffix = lower_suffix(file_name);
      auto stem = file_name.left(file_name.size() - suffix.size() - 1);
      const bool double_size = stem.endsWith(QStringLiteral("@2x"));
      if (double_size) {
        stem.chop(3);
      }
      if (!stems.contains(stem)) {
        result.warnings.push_back(
            QCoreApplication::translate("ThemeFile", "Icon \"%1\" matches no built-in icon (ignored).").arg(file_name));
        continue;
      }
      if (suffix == QStringLiteral("svg") && !double_size) {
        if (!is_safe_theme_svg(bytes)) {
          result.warnings.push_back(
              QCoreApplication::translate("ThemeFile",
                                          "Icon \"%1\" refers to files or addresses outside itself (ignored).")
                  .arg(file_name));
          continue;
        }
        assets->icons[stem].svg = bytes;
        continue;
      }
      if (suffix != QStringLiteral("png") && suffix != QStringLiteral("bmp")) {
        result.warnings.push_back(
            QCoreApplication::translate("ThemeFile", "Icon \"%1\" is not an SVG, PNG or BMP file (ignored).")
                .arg(file_name));
        continue;
      }
      const auto image = decode_package_image(bytes, suffix, limits.max_icon_side);
      if (image.isNull()) {
        result.warnings.push_back(
            QCoreApplication::translate("ThemeFile", "Icon \"%1\" could not be read (ignored).").arg(file_name));
        continue;
      }
      auto& icon = assets->icons[stem];
      (double_size ? icon.image_2x : icon.image) = image;
    } else if (path.startsWith(QStringLiteral("sounds/"))) {
      const auto file_name = path.mid(7);
      if (file_name.contains(QLatin1Char('/')) || lower_suffix(file_name) != QStringLiteral("wav")) {
        continue;
      }
      const auto id = file_name.left(file_name.size() - 4);
      if (!theme_sound_event_from_id(id)) {
        result.warnings.push_back(
            QCoreApplication::translate("ThemeFile", "Sound \"%1\" names no sound event (ignored).").arg(file_name));
        continue;
      }
      if (!is_wav(bytes)) {
        result.warnings.push_back(
            QCoreApplication::translate("ThemeFile", "Sound \"%1\" is not a WAV file (ignored).").arg(file_name));
        continue;
      }
      assets->sounds.insert(id, bytes);
    }
  }

  const auto& backdrop_path = assets->options.backdrop_image;
  if (!backdrop_path.isEmpty()) {
    if (!is_safe_theme_package_path(backdrop_path) || !files.contains(backdrop_path)) {
      result.warnings.push_back(
          QCoreApplication::translate("ThemeFile", "Backdrop image \"%1\" is not in the package (ignored).")
              .arg(backdrop_path));
    } else {
      assets->backdrop =
          decode_package_image(files.value(backdrop_path), lower_suffix(backdrop_path), limits.max_backdrop_side);
      if (assets->backdrop.isNull()) {
        result.warnings.push_back(
            QCoreApplication::translate("ThemeFile", "Backdrop image \"%1\" could not be read (ignored).")
                .arg(backdrop_path));
      }
    }
  }

  assets->manifest = QJsonDocument::fromJson(manifest).object();
  parse_event_actions(*assets, files, result.warnings);

  assets->files = std::move(files);
  result.theme->assets = std::move(assets);
  return result;
}

[[nodiscard]] ThemeLoadResult package_error(const QString& message) {
  ThemeLoadResult result;
  result.error = message;
  return result;
}

[[nodiscard]] bool is_package_folder_entry(const QString& relative) {
  return relative == kThemePackageManifest || relative.startsWith(QStringLiteral("icons/")) ||
         relative.startsWith(QStringLiteral("images/")) || relative.startsWith(QStringLiteral("sounds/")) ||
         relative.startsWith(QStringLiteral("scripts/"));
}

// The *_summary loaders' result from theme.json and `read` (a package file's bytes by
// relative path, empty when missing): only the backdrop is read besides the manifest.
template <typename Read>
ThemeLoadResult load_theme_package_summary(const QByteArray& manifest, const Read& read,
                                           const ThemePackageLimits& limits) {
  if (manifest.isEmpty()) {
    return package_error(tr_theme(QT_TRANSLATE_NOOP("ThemeFile", "The theme package has no theme.json.")));
  }
  auto result = load_theme_from_json(manifest);
  if (!result.theme) {
    return result;
  }
  auto assets = std::make_shared<ThemeAssets>();
  assets->summary = true;
  assets->options = result.theme->options;
  assets->info = result.theme->info;
  assets->manifest = QJsonDocument::fromJson(manifest).object();
  const auto& backdrop_path = assets->options.backdrop_image;
  if (is_safe_theme_package_path(backdrop_path) && is_package_folder_entry(backdrop_path) &&
      backdrop_path != kThemePackageManifest) {
    assets->backdrop = decode_package_image(read(backdrop_path), lower_suffix(backdrop_path), limits.max_backdrop_side);
  }
  result.theme->assets = std::move(assets);
  return result;
}

[[nodiscard]] bool is_inside_canonical_root(const QString& root_canonical, const QString& absolute) {
  const auto canonical = QFileInfo(absolute).canonicalFilePath();
  return !canonical.isEmpty() && canonical.startsWith(root_canonical + QLatin1Char('/'));
}

ThemeLoadResult load_zip(const QByteArray& zip, const ThemePackageLimits& limits, bool summary) {
  if (zip.size() > limits.max_package_bytes) {
    return package_error(tr_theme(QT_TRANSLATE_NOOP("ThemeFile", "The theme package is larger than the theme package limits.")));
  }
  static constexpr formats::ZipReaderMessages kMessages{
      QT_TRANSLATE_NOOP("ThemeFile", "The theme package is not a readable zip file."),
      QT_TRANSLATE_NOOP("ThemeFile", "The theme package is larger than the theme package limits."),
      QT_TRANSLATE_NOOP("ThemeFile", "The theme package uses encryption or a compression method Patchy cannot read."),
      QT_TRANSLATE_NOOP("ThemeFile", "A file in the theme package is damaged."),
  };
  QHash<QString, QByteArray> files;
  try {
    formats::ZipEntryReader reader(
        std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(zip.constData()),
                                      static_cast<std::size_t>(zip.size())),
        kMessages,
        formats::ZipReaderLimits{static_cast<std::uint64_t>(limits.max_entry_bytes),
                                 static_cast<std::uint64_t>(limits.max_total_bytes),
                                 static_cast<std::size_t>(limits.max_entries)});
    const auto names = reader.entry_names();
    // Every name is checked, not only the ones read: a package carrying a path that
    // climbs out of it is refused as a whole.
    for (const auto& name : names) {
      if (!formats::safe_zip_entry_path(name) || !is_safe_theme_package_path(QString::fromUtf8(name))) {
        return package_error(
            QCoreApplication::translate("ThemeFile", "The theme package holds a path outside the package: %1")
                .arg(QString::fromUtf8(name)));
      }
    }
    // theme.json at the root, or inside the one top folder a zipped folder has.
    QString prefix;
    if (!reader.contains(std::string(kThemePackageManifest.data(), kThemePackageManifest.size()))) {
      QStringList candidates;
      for (const auto& name : names) {
        const auto pieces = QString::fromUtf8(name).split(QLatin1Char('/'));
        if (pieces.size() == 2 && pieces[1] == kThemePackageManifest) {
          candidates.push_back(pieces[0] + QLatin1Char('/'));
        }
      }
      if (candidates.size() != 1) {
        return package_error(tr_theme(QT_TRANSLATE_NOOP("ThemeFile", "The theme package has no theme.json.")));
      }
      prefix = candidates.front();
    }
    if (summary) {
      const auto read = [&reader, &prefix, &limits](const QString& relative) {
        const auto name = (prefix + relative).toStdString();
        if (!reader.contains(name)) {
          return QByteArray();
        }
        const auto bytes = reader.extract(name, static_cast<std::uint64_t>(limits.max_entry_bytes));
        return bytes ? QByteArray(reinterpret_cast<const char*>(bytes->data()), static_cast<qsizetype>(bytes->size()))
                     : QByteArray();
      };
      return load_theme_package_summary(read(QString(kThemePackageManifest)), read, limits);
    }
    for (const auto& name : names) {
      const auto path = QString::fromUtf8(name);
      if (!path.startsWith(prefix)) {
        continue;
      }
      const auto relative = path.mid(prefix.size());
      if (!is_package_folder_entry(relative)) {
        continue;
      }
      auto bytes = reader.extract(name, static_cast<std::uint64_t>(limits.max_entry_bytes));
      if (bytes) {
        files.insert(relative, QByteArray(reinterpret_cast<const char*>(bytes->data()),
                                          static_cast<qsizetype>(bytes->size())));
      }
    }
  } catch (const std::runtime_error& error) {
    return package_error(tr_theme(error.what()));
  }
  return load_theme_package_files(std::move(files), limits);
}

}  // namespace

bool ThemeAssets::has_scripts() const { return files.contains(QStringLiteral("scripts/main.js")); }

QImage theme_package_image(const ThemeAssets& assets, const QString& path, int max_side) {
  if (!is_safe_theme_package_path(path)) {
    return {};
  }
  const auto found = assets.files.constFind(path);
  return found == assets.files.constEnd() ? QImage() : decode_package_image(found.value(), lower_suffix(path), max_side);
}

bool is_theme_package_zip_name(const QString& file_name) {
  return file_name.endsWith(QStringLiteral(".zip"), Qt::CaseInsensitive);
}

bool is_safe_theme_svg(const QByteArray& svg) {
  // url( that does not go straight to a "#fragment", quoted or not (possessive, so the
  // spaces cannot give back a character to dodge the lookahead).
  static const QRegularExpression outside_url(QStringLiteral("url\\s*\\(\\s*+(?![\"']?\\s*#)"),
                                              QRegularExpression::CaseInsensitiveOption);
  static const QRegularExpression embedded_image(QStringLiteral("^data:image/(png|jpeg|jpg);"),
                                                 QRegularExpression::CaseInsensitiveOption);
  const auto reaches_outside = [](QStringView text) {
    return outside_url.matchView(text).hasMatch() || text.contains(QStringLiteral("@import"), Qt::CaseInsensitive);
  };
  QXmlStreamReader reader(svg);
  while (!reader.atEnd()) {
    switch (reader.readNext()) {
      case QXmlStreamReader::DTD:
      case QXmlStreamReader::EntityReference:
      case QXmlStreamReader::ProcessingInstruction:
        return false;
      case QXmlStreamReader::StartElement: {
        const bool image = reader.name().compare(QStringLiteral("image"), Qt::CaseInsensitive) == 0;
        bool image_has_data = false;
        for (const auto& attribute : reader.attributes()) {
          const auto value = attribute.value().trimmed();
          if (attribute.name().compare(QStringLiteral("href"), Qt::CaseInsensitive) == 0) {
            if (image && embedded_image.matchView(value).hasMatch()) {
              image_has_data = true;
            } else if (!value.startsWith(QLatin1Char('#'))) {
              return false;
            }
          } else if (reaches_outside(value)) {
            return false;
          }
        }
        if (image && !image_has_data) {
          return false;
        }
        break;
      }
      case QXmlStreamReader::Characters:
        // <style> text, and anything else a renderer might read as CSS.
        if (reaches_outside(reader.text())) {
          return false;
        }
        break;
      default:
        break;
    }
  }
  return !reader.hasError();
}

bool is_safe_theme_package_path(const QString& path) {
  if (path.isEmpty() || path.startsWith(QLatin1Char('/')) || path.contains(QLatin1Char('\\')) ||
      path.contains(QLatin1Char(':'))) {
    return false;
  }
  const auto pieces = path.split(QLatin1Char('/'));
  return std::none_of(pieces.begin(), pieces.end(), [](const QString& piece) { return piece == QStringLiteral(".."); });
}

ThemeLoadResult load_theme_package_directory(const QString& directory, const ThemePackageLimits& limits) {
  const QDir root(QFileInfo(directory).absoluteFilePath());
  const auto root_canonical = QFileInfo(directory).canonicalFilePath();
  if (root_canonical.isEmpty()) {
    return package_error(
        QCoreApplication::translate("ThemeFile", "Theme folder \"%1\" could not be read.").arg(directory));
  }
  const auto inside_root = [&root_canonical](const QString& absolute) {
    return is_inside_canonical_root(root_canonical, absolute);
  };
  const auto escapes = [&directory](const QString& relative) {
    return package_error(
        QCoreApplication::translate("ThemeFile", "Theme package \"%1\" holds a path outside the package: %2")
            .arg(QFileInfo(directory).fileName(), relative));
  };
  const auto too_large = [&directory] {
    return package_error(
        QCoreApplication::translate("ThemeFile", "Theme package \"%1\" is larger than the theme package limits.")
            .arg(QFileInfo(directory).fileName()));
  };

  QHash<QString, QByteArray> files;
  qint64 total = 0;
  QStringList relatives{QString(kThemePackageManifest)};
  for (const auto& folder :
       {QStringLiteral("icons"), QStringLiteral("images"), QStringLiteral("sounds"), QStringLiteral("scripts")}) {
    const auto folder_path = root.filePath(folder);
    if (!QFileInfo(folder_path).isDir()) {
      continue;
    }
    if (!inside_root(folder_path)) {
      return escapes(folder);
    }
    QDirIterator it(folder_path, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
      relatives.push_back(root.relativeFilePath(it.next()));
      if (relatives.size() > limits.max_entries) {
        return too_large();
      }
    }
  }
  for (const auto& relative : relatives) {
    if (!is_safe_theme_package_path(relative)) {
      return escapes(relative);
    }
    const auto absolute = root.filePath(relative);
    if (!inside_root(absolute)) {
      return escapes(relative);
    }
    QFile file(absolute);
    if (!file.open(QIODevice::ReadOnly)) {
      if (relative == kThemePackageManifest) {
        return package_error(
            QCoreApplication::translate("ThemeFile", "Theme folder \"%1\" has no readable theme.json.")
                .arg(QFileInfo(directory).fileName()));
      }
      continue;
    }
    if (file.size() > limits.max_entry_bytes || total + file.size() > limits.max_total_bytes) {
      return too_large();
    }
    auto bytes = file.read(limits.max_entry_bytes + 1);
    if (bytes.size() > limits.max_entry_bytes) {
      return too_large();
    }
    total += bytes.size();
    files.insert(relative, std::move(bytes));
  }
  return load_theme_package_files(std::move(files), limits);
}

ThemeLoadResult load_theme_package_directory_summary(const QString& directory, const ThemePackageLimits& limits) {
  const QDir root(QFileInfo(directory).absoluteFilePath());
  const auto root_canonical = QFileInfo(directory).canonicalFilePath();
  if (root_canonical.isEmpty()) {
    return package_error(
        QCoreApplication::translate("ThemeFile", "Theme folder \"%1\" could not be read.").arg(directory));
  }
  const auto read = [&root, &root_canonical, &limits](const QString& relative) {
    const auto absolute = root.filePath(relative);
    QFile file(absolute);
    if (!is_inside_canonical_root(root_canonical, absolute) || !file.open(QIODevice::ReadOnly) ||
        file.size() > limits.max_entry_bytes) {
      return QByteArray();
    }
    return file.read(limits.max_entry_bytes);
  };
  const auto manifest = read(QString(kThemePackageManifest));
  if (manifest.isEmpty()) {
    return package_error(QCoreApplication::translate("ThemeFile", "Theme folder \"%1\" has no readable theme.json.")
                             .arg(QFileInfo(directory).fileName()));
  }
  return load_theme_package_summary(manifest, read, limits);
}

ThemeLoadResult load_theme_package_zip(const QByteArray& zip, const ThemePackageLimits& limits) {
  return load_zip(zip, limits, /*summary=*/false);
}

ThemeLoadResult load_theme_package_zip_summary(const QByteArray& zip, const ThemePackageLimits& limits) {
  return load_zip(zip, limits, /*summary=*/true);
}

ThemeLoadResult load_theme_package_zip_file(const QString& path, const ThemePackageLimits& limits) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    return package_error(QCoreApplication::translate("ThemeFile", "Theme file \"%1\" could not be read.")
                             .arg(QFileInfo(path).fileName()));
  }
  if (file.size() > limits.max_package_bytes) {
    return package_error(tr_theme(QT_TRANSLATE_NOOP("ThemeFile", "The theme package is larger than the theme package limits.")));
  }
  return load_theme_package_zip(file.readAll(), limits);
}

QByteArray serialize_theme_package_zip(const ThemePalette& palette, ColorScheme base, const QString& name,
                                       const ThemeAssets* assets) {
  std::vector<ora::detail::ZipEntry> entries;
  const auto manifest = serialize_theme_to_json(palette, base, name, assets != nullptr ? &assets->options : nullptr,
                                                assets != nullptr ? &assets->info : nullptr);
  entries.push_back({std::string(kThemePackageManifest.data(), kThemePackageManifest.size()),
                     to_bytes(manifest), true});
  if (assets != nullptr) {
    auto paths = assets->files.keys();
    paths.sort();
    for (const auto& path : paths) {
      const auto& bytes = assets->files[path];
      const auto suffix = lower_suffix(path);
      // PNG and JPEG are compressed already; deflating them again only costs time.
      const bool deflate = suffix != QStringLiteral("png") && suffix != QStringLiteral("jpg") &&
                           suffix != QStringLiteral("jpeg");
      entries.push_back({path.toStdString(), to_bytes(bytes), deflate});
    }
  }
  const auto zip = ora::detail::zip_archive(entries);
  return QByteArray(reinterpret_cast<const char*>(zip.data()), static_cast<qsizetype>(zip.size()));
}

QStringList overridable_icon_stems() {
  auto stems = builtin_icon_stems();
  for (const auto& excluded : stylesheet_referenced_icon_names()) {
    stems.removeAll(excluded);
  }
  return stems;
}

int export_icon_template(const QString& directory, QString* error) {
  const auto fail = [error](const QString& message) {
    if (error != nullptr) {
      *error = message;
    }
    return -1;
  };
  if (directory.isEmpty() || !QDir().mkpath(directory)) {
    return fail(QCoreApplication::translate("ThemeFile", "Could not create the folder \"%1\".").arg(directory));
  }
  const QDir dir(directory);
  int written = 0;
  for (const auto& stem : overridable_icon_stems()) {
    const auto svg = builtin_icon_svg(stem);
    QSaveFile file(dir.filePath(stem + QStringLiteral(".svg")));
    if (svg.isEmpty() || !file.open(QIODevice::WriteOnly) || file.write(svg) != svg.size() || !file.commit()) {
      return fail(QCoreApplication::translate("ThemeFile", "Could not write \"%1\".").arg(file.fileName()));
    }
    ++written;
  }
  return written;
}

void set_active_theme_assets(std::shared_ptr<const ThemeAssets> assets) { active_assets_slot() = std::move(assets); }

const ThemeAssets* active_theme_assets() { return active_assets_slot().get(); }

std::shared_ptr<const ThemeAssets> active_theme_assets_shared() { return active_assets_slot(); }

}  // namespace patchy::ui
