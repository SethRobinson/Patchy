#include "ui/pattern_library.hpp"

#include "core/pattern_presets.hpp"
#include "psd/pat_reader.hpp"
#include "ui/app_settings.hpp"
#include "ui/photo_pattern_presets.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QSaveFile>
#include <QUuid>

#include <cstdint>

#include <algorithm>
#include <cstring>
#include <exception>
#include <functional>
#include <span>
#include <string>
#include <utility>

namespace patchy::ui {

namespace {

constexpr std::size_t kTileCacheLimit = 16;
constexpr std::size_t kTileCacheByteLimit = 64U * 1024U * 1024U;
constexpr int kInitialBuiltinPatternVersion = 1;
constexpr qint64 kMaxPatFileBytes = 32LL * 1024LL * 1024LL;
// Mirrors pat_reader's per-pattern decode cap.
constexpr std::int64_t kMaxImagePatternPixels = 8LL * 1024LL * 1024LL;
constexpr int kMaxPatternDimension = 30'000;

[[nodiscard]] QString default_storage_dir() {
  const auto settings = app_settings();
  return QFileInfo(settings.fileName()).absolutePath() + QStringLiteral("/patterns");
}

[[nodiscard]] QString qstring_from_utf8(const std::string& text) {
  return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

[[nodiscard]] std::string utf8_from_qstring(const QString& text) {
  const auto bytes = text.toUtf8();
  return std::string(bytes.constData(), static_cast<std::size_t>(bytes.size()));
}

[[nodiscard]] QImage image_from_pattern_tile(const PixelBuffer& tile) {
  if (tile.empty() || tile.format() != PixelFormat::rgba8() || tile.width() <= 0 ||
      tile.height() <= 0) {
    return {};
  }
  QImage image(tile.width(), tile.height(), QImage::Format_RGBA8888);
  if (image.isNull()) {
    return {};
  }
  const auto row_bytes = static_cast<std::size_t>(tile.width()) * 4U;
  for (std::int32_t y = 0; y < tile.height(); ++y) {
    std::memcpy(image.scanLine(y), tile.row(y).data(), row_bytes);
  }
  return image;
}

[[nodiscard]] std::optional<PixelBuffer> pattern_tile_from_image(const QImage& source) {
  if (source.isNull() || source.width() <= 0 || source.height() <= 0) {
    return std::nullopt;
  }
  const auto image = source.convertToFormat(QImage::Format_RGBA8888);
  if (image.isNull()) {
    return std::nullopt;
  }
  try {
    PixelBuffer tile(image.width(), image.height(), PixelFormat::rgba8());
    const auto row_bytes = static_cast<std::size_t>(image.width()) * 4U;
    for (int y = 0; y < image.height(); ++y) {
      std::memcpy(tile.row(y).data(), image.constScanLine(y), row_bytes);
    }
    return tile;
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

[[nodiscard]] bool pattern_tiles_equal(const PixelBuffer& lhs, const PixelBuffer& rhs) {
  return lhs.width() == rhs.width() && lhs.height() == rhs.height() &&
         lhs.format() == rhs.format() && lhs.data().size() == rhs.data().size() &&
         std::equal(lhs.data().begin(), lhs.data().end(), rhs.data().begin());
}

[[nodiscard]] bool save_png(const QImage& image, const QString& path) {
  if (image.isNull()) {
    return false;
  }
  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly) || !image.save(&file, "PNG")) {
    return false;
  }
  return file.commit();
}

[[nodiscard]] QString fresh_pattern_id() {
  return qstring_from_utf8(generate_pattern_uuid());
}

}  // namespace

QString default_patterns_folder_name() {
  return QObject::tr("Patchy Defaults");
}

QString photo_patterns_folder_name() {
  return QObject::tr("Textures");
}

QString pattern_library_entry_display_name(const PatternLibraryEntry& entry) {
  const auto id = utf8_from_qstring(entry.id);
  const char* canonical_english = nullptr;
  if (const auto* preset = find_builtin_pattern_preset(id); preset != nullptr) {
    canonical_english = preset->english_name;
  } else if (const auto* photo = find_photo_pattern_preset(id); photo != nullptr) {
    canonical_english = photo->english_name;
  }
  if (canonical_english != nullptr && entry.name == QString::fromLatin1(canonical_english)) {
    // Keep the existing QObject translation context used by the layer-style
    // pattern picker and the Japanese pattern-name translations.
    return QCoreApplication::translate("QObject", canonical_english);
  }
  return entry.name;
}

QPixmap pattern_thumbnail(const PixelBuffer& tile, int extent) {
  extent = std::max(8, extent);
  QImage canvas(extent, extent, QImage::Format_ARGB32_Premultiplied);
  canvas.fill(Qt::transparent);
  QPainter painter(&canvas);

  constexpr int kCheckerSize = 6;
  for (int y = 0; y < extent; y += kCheckerSize) {
    for (int x = 0; x < extent; x += kCheckerSize) {
      const auto light = ((x / kCheckerSize) + (y / kCheckerSize)) % 2 == 0;
      painter.fillRect(QRect(x, y, kCheckerSize, kCheckerSize),
                       light ? QColor(218, 218, 218) : QColor(174, 174, 174));
    }
  }

  const auto source = image_from_pattern_tile(tile);
  if (!source.isNull()) {
    // Always show at least a 2x2 repeat so seams and direction are visible even
    // for a large source tile. Keep rectangular tiles rectangular.
    const auto repeat_extent = std::max(4, extent / 2);
    const auto repeated_tile = source.scaled(repeat_extent, repeat_extent, Qt::KeepAspectRatio,
                                             Qt::SmoothTransformation);
    if (!repeated_tile.isNull()) {
      painter.setClipRect(QRect(1, 1, extent - 2, extent - 2));
      painter.drawTiledPixmap(QRect(1, 1, extent - 2, extent - 2),
                              QPixmap::fromImage(repeated_tile));
      painter.setClipping(false);
    }
  }
  painter.setPen(QColor(0, 0, 0, 90));
  painter.setBrush(Qt::NoBrush);
  painter.drawRect(0, 0, extent - 1, extent - 1);
  painter.end();
  return QPixmap::fromImage(canvas);
}

PatternLibrary::PatternLibrary(QString storage_dir, QObject* parent)
    : QObject(parent),
      storage_dir_(storage_dir.isEmpty() ? default_storage_dir() : std::move(storage_dir)) {
  reload();
}

const QString& PatternLibrary::storage_dir() const noexcept {
  return storage_dir_;
}

const std::vector<PatternLibraryEntry>& PatternLibrary::entries() const noexcept {
  return entries_;
}

const PatternLibraryEntry* PatternLibrary::find_entry(const QString& storage_id) const {
  const auto found = std::find_if(entries_.begin(), entries_.end(),
                                  [&storage_id](const PatternLibraryEntry& entry) {
                                    return entry.storage_id == storage_id;
                                  });
  return found == entries_.end() ? nullptr : &*found;
}

const PatternLibraryEntry* PatternLibrary::find_entry_by_pattern_id(const QString& id) const {
  const auto found = std::find_if(entries_.begin(), entries_.end(),
                                  [&id](const PatternLibraryEntry& entry) {
                                    return entry.id == id;
                                  });
  return found == entries_.end() ? nullptr : &*found;
}

QString PatternLibrary::png_path(const QString& storage_id) const {
  return storage_dir_ + QStringLiteral("/") + storage_id + QStringLiteral(".png");
}

QString PatternLibrary::json_path(const QString& storage_id) const {
  return storage_dir_ + QStringLiteral("/") + storage_id + QStringLiteral(".json");
}

void PatternLibrary::reload() {
  entries_.clear();
  tile_cache_.clear();
  const QDir dir(storage_dir_);
  if (!dir.exists()) {
    return;
  }

  const auto png_files = dir.entryInfoList({QStringLiteral("*.png")}, QDir::Files, QDir::Name);
  for (const auto& file_info : png_files) {
    const auto storage_id = file_info.completeBaseName();
    const QImage image(file_info.absoluteFilePath());
    const auto tile = pattern_tile_from_image(image);
    if (!tile.has_value()) {
      continue;  // one corrupt PNG never hides the rest of the library
    }

    PatternLibraryEntry entry;
    entry.storage_id = storage_id;
    entry.id = storage_id;
    entry.name = storage_id;
    QFile sidecar(json_path(storage_id));
    if (sidecar.open(QIODevice::ReadOnly)) {
      const auto document = QJsonDocument::fromJson(sidecar.readAll());
      if (document.isObject()) {
        const auto object = document.object();
        const auto id = object.value(QStringLiteral("id")).toString();
        const auto name = object.value(QStringLiteral("name")).toString().trimmed();
        if (!id.isEmpty()) {
          entry.id = id;
        }
        entry.source_id = object.value(QStringLiteral("sourceId")).toString();
        if (!name.isEmpty()) {
          entry.name = name;
        }
        entry.folder = object.value(QStringLiteral("folder")).toString().trimmed();
      }
    }
    if (entry.id.isEmpty() || find_entry_by_pattern_id(entry.id) != nullptr) {
      continue;  // ids are unique within the installed library; first file wins
    }
    entry.size = image.size();
    entry.thumbnail = pattern_thumbnail(*tile);
    entries_.push_back(std::move(entry));
  }
  sort_entries();
}

void PatternLibrary::sort_entries() {
  std::sort(entries_.begin(), entries_.end(),
            [](const PatternLibraryEntry& a, const PatternLibraryEntry& b) {
              if (a.folder.isEmpty() != b.folder.isEmpty()) {
                return a.folder.isEmpty();
              }
              const auto folder_order = QString::compare(a.folder, b.folder, Qt::CaseInsensitive);
              if (folder_order != 0) {
                return folder_order < 0;
              }
              const auto name_order = QString::compare(a.name, b.name, Qt::CaseInsensitive);
              if (name_order != 0) {
                return name_order < 0;
              }
              return a.storage_id < b.storage_id;
            });
}

QStringList PatternLibrary::folders() const {
  QStringList result;
  for (const auto& entry : entries_) {
    if (!entry.folder.isEmpty() && !result.contains(entry.folder)) {
      result.append(entry.folder);
    }
  }
  return result;
}

bool PatternLibrary::write_sidecar(const PatternLibraryEntry& entry) const {
  QJsonObject object;
  object.insert(QStringLiteral("id"), entry.id);
  if (!entry.source_id.isEmpty()) {
    object.insert(QStringLiteral("sourceId"), entry.source_id);
  }
  object.insert(QStringLiteral("name"), entry.name);
  if (!entry.folder.trimmed().isEmpty()) {
    object.insert(QStringLiteral("folder"), entry.folder.trimmed());
  }
  const auto bytes = QJsonDocument(object).toJson(QJsonDocument::Indented);
  QSaveFile file(json_path(entry.storage_id));
  if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()) {
    return false;
  }
  return file.commit();
}

void PatternLibrary::invalidate_cached_tile(const QString& storage_id) const {
  tile_cache_.erase(std::remove_if(tile_cache_.begin(), tile_cache_.end(),
                                   [&storage_id](const auto& cached) {
                                     return cached.first == storage_id;
                                   }),
                    tile_cache_.end());
}

void PatternLibrary::cache_tile(const QString& storage_id, PixelBuffer tile) const {
  invalidate_cached_tile(storage_id);
  const auto tile_bytes = tile.byte_size();
  if (tile_bytes > kTileCacheByteLimit) {
    return;
  }
  const auto cached_bytes = [&] {
    std::size_t result = 0;
    for (const auto& cached : tile_cache_) {
      result += cached.second.byte_size();
    }
    return result;
  };
  while (!tile_cache_.empty() &&
         (tile_cache_.size() >= kTileCacheLimit ||
          cached_bytes() > kTileCacheByteLimit - tile_bytes)) {
    tile_cache_.erase(tile_cache_.begin());
  }
  tile_cache_.emplace_back(storage_id, std::move(tile));
}

std::optional<PixelBuffer> PatternLibrary::tile_for_entry(
    const PatternLibraryEntry& entry) const {
  for (const auto& cached : tile_cache_) {
    if (cached.first == entry.storage_id) {
      return cached.second;
    }
  }
  const auto loaded = pattern_tile_from_image(QImage(png_path(entry.storage_id)));
  if (!loaded.has_value()) {
    return std::nullopt;
  }
  cache_tile(entry.storage_id, *loaded);
  return loaded;
}

std::optional<PatternResource> PatternLibrary::resource(const QString& pattern_id) const {
  const auto* entry = find_entry_by_pattern_id(pattern_id);
  if (entry == nullptr) {
    return std::nullopt;
  }
  return resource_for_entry(entry->storage_id);
}

std::optional<PatternResource> PatternLibrary::resource_for_entry(
    const QString& storage_id) const {
  const auto* entry = find_entry(storage_id);
  if (entry == nullptr) {
    return std::nullopt;
  }
  auto tile = tile_for_entry(*entry);
  if (!tile.has_value()) {
    return std::nullopt;
  }
  PatternResource result;
  result.id = utf8_from_qstring(entry->id);
  result.name = utf8_from_qstring(entry->name);
  result.tile = std::move(*tile);
  result.provenance = PatternProvenance::Authored;
  return result;
}

QString PatternLibrary::add_pattern_internal(const QString& name, const PixelBuffer& tile,
                                             const QString& folder,
                                             const QString& requested_pattern_id,
                                             const QString& source_id) {
  if (tile.empty() || tile.format() != PixelFormat::rgba8() ||
      tile.width() > kMaxPatternDimension || tile.height() > kMaxPatternDimension) {
    return {};
  }
  auto pattern_id = requested_pattern_id;
  if (pattern_id.isEmpty()) {
    do {
      pattern_id = fresh_pattern_id();
    } while (find_entry_by_pattern_id(pattern_id) != nullptr);
  } else if (find_entry_by_pattern_id(pattern_id) != nullptr) {
    return {};
  }
  if (!QDir().mkpath(storage_dir_)) {
    return {};
  }

  QString storage_id;
  do {
    storage_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
  } while (QFileInfo::exists(png_path(storage_id)) || QFileInfo::exists(json_path(storage_id)));

  PatternLibraryEntry entry;
  entry.storage_id = storage_id;
  entry.id = pattern_id;
  entry.source_id = source_id;
  entry.name = name.trimmed().isEmpty() ? tr("Untitled Pattern") : name.trimmed();
  entry.folder = folder.trimmed();
  entry.size = QSize(tile.width(), tile.height());
  entry.thumbnail = pattern_thumbnail(tile);
  if (!write_sidecar(entry)) {
    return {};
  }
  const auto image = image_from_pattern_tile(tile);
  if (!save_png(image, png_path(storage_id))) {
    // reload() is PNG-driven, so a sidecar left behind by a failed cleanup can
    // never resurrect a pattern that was not fully saved.
    (void)QFile::remove(json_path(storage_id));
    return {};
  }
  entries_.push_back(entry);
  cache_tile(storage_id, tile);
  return storage_id;
}

QString PatternLibrary::add_pattern(const QString& name, const PixelBuffer& tile,
                                    const QString& folder,
                                    const QString& preferred_pattern_id) {
  const auto storage_id = add_pattern_internal(name, tile, folder, preferred_pattern_id);
  if (!storage_id.isEmpty()) {
    sort_entries();
    emit changed();
  }
  return storage_id;
}

QString PatternLibrary::import_pat(const QString& path, QString& error, QStringList& warnings) {
  error.clear();
  warnings.clear();
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    error = tr("Could not open \"%1\".").arg(QFileInfo(path).fileName());
    return {};
  }
  const auto file_size = file.size();
  if (file_size < 0) {
    error = tr("Could not read \"%1\".").arg(QFileInfo(path).fileName());
    return {};
  }
  if (file_size > kMaxPatFileBytes) {
    error = tr("\"%1\" is too large to import safely.").arg(QFileInfo(path).fileName());
    return {};
  }
  const auto bytes = file.readAll();
  if (file.error() != QFileDevice::NoError || bytes.size() != file_size) {
    error = tr("Could not read \"%1\".").arg(QFileInfo(path).fileName());
    return {};
  }
  std::string parse_error;
  const auto parsed = psd::read_pat(
      std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(bytes.constData()),
                                    static_cast<std::size_t>(bytes.size())),
      parse_error);
  if (!parsed.has_value()) {
    error = tr("Could not import patterns from \"%1\". The file is not a supported Photoshop PAT file "
               "or is damaged.")
                .arg(QFileInfo(path).fileName());
    return {};
  }
  if (!parsed->warnings.empty()) {
    warnings.append(
        tr("Some pattern data was skipped or repaired because it is unsupported or damaged."));
  }

  auto folder = QFileInfo(path).completeBaseName().trimmed();
  if (folder.isEmpty()) {
    folder = tr("Imported Patterns");
  }
  QString first_storage_id;
  int added = 0;
  int pattern_index = 0;
  for (const auto& imported : parsed->patterns) {
    ++pattern_index;
    auto name = qstring_from_utf8(imported.name).trimmed();
    if (name.isEmpty()) {
      name = tr("Pattern %1").arg(pattern_index);
    }
    if (imported.tile.empty() || imported.tile.format() != PixelFormat::rgba8()) {
      warnings.append(tr("Skipped pattern \"%1\" because its pixels could not be decoded.").arg(name));
      continue;
    }

    const auto source_pattern_id = qstring_from_utf8(imported.id);
    auto pattern_id = source_pattern_id;
    if (pattern_id.isEmpty()) {
      pattern_id = fresh_pattern_id();
    }

    const PatternLibraryEntry* matching_entry = nullptr;
    const auto* exact_id_entry = find_entry_by_pattern_id(pattern_id);
    if (exact_id_entry != nullptr) {
      const auto existing_resource = resource_for_entry(exact_id_entry->storage_id);
      if (existing_resource.has_value() &&
          pattern_tiles_equal(existing_resource->tile, imported.tile)) {
        matching_entry = exact_id_entry;
      }
    }
    if (matching_entry == nullptr && !source_pattern_id.isEmpty()) {
      for (const auto& candidate : entries_) {
        if (candidate.source_id != source_pattern_id) {
          continue;
        }
        const auto candidate_resource = resource_for_entry(candidate.storage_id);
        if (candidate_resource.has_value() &&
            pattern_tiles_equal(candidate_resource->tile, imported.tile)) {
          matching_entry = &candidate;
          break;
        }
      }
    }
    if (matching_entry != nullptr) {
      if (first_storage_id.isEmpty()) {
        first_storage_id = matching_entry->storage_id;
      }
      continue;  // same source identity and pixels: already installed
    }

    QString remapped_source_id;
    if (exact_id_entry != nullptr) {
      do {
        pattern_id = fresh_pattern_id();
      } while (find_entry_by_pattern_id(pattern_id) != nullptr);
      remapped_source_id = source_pattern_id;
      warnings.append(
          tr("Pattern \"%1\" used an id already assigned to different pixels; it was imported with a new id.")
              .arg(name));
    }

    const auto storage_id =
        add_pattern_internal(name, imported.tile, folder, pattern_id, remapped_source_id);
    if (storage_id.isEmpty()) {
      warnings.append(tr("Could not save pattern \"%1\".").arg(name));
      continue;
    }
    ++added;
    if (first_storage_id.isEmpty()) {
      first_storage_id = storage_id;
    }
  }
  if (first_storage_id.isEmpty()) {
    error = tr("No patterns could be imported from \"%1\".").arg(QFileInfo(path).fileName());
    return {};
  }
  if (added > 0) {
    sort_entries();
    emit changed();
  }
  return first_storage_id;
}

QString PatternLibrary::import_image(const QString& path, QString& error, QStringList& warnings) {
  error.clear();
  warnings.clear();
  const auto file_name = QFileInfo(path).fileName();
  QImageReader reader(path);
  reader.setAutoTransform(true);  // apply EXIF orientation (phone photos)
  const auto exceeds_pixel_cap = [](QSize size) {
    return size.isValid() &&
           static_cast<std::int64_t>(size.width()) * size.height() > kMaxImagePatternPixels;
  };
  const auto exceeds_dimension_cap = [](QSize size) {
    return size.isValid() &&
           (size.width() > kMaxPatternDimension || size.height() > kMaxPatternDimension);
  };
  // Reject oversized images from their header before asking the image plugin to
  // allocate and decode the full raster. Keep the post-read check below for
  // formats whose handlers cannot report dimensions up front.
  const auto header_size = reader.size();
  if (exceeds_dimension_cap(header_size)) {
    error = tr("\"%1\" is too large to use as a pattern (over 30,000 pixels wide or tall).")
                .arg(file_name);
    return {};
  }
  if (exceeds_pixel_cap(header_size)) {
    error = tr("\"%1\" is too large to use as a pattern (over 8 million pixels).").arg(file_name);
    return {};
  }
  const auto image = reader.read();
  if (image.isNull()) {
    error = tr("Could not read \"%1\" as an image.").arg(file_name);
    return {};
  }
  if (exceeds_dimension_cap(image.size())) {
    error = tr("\"%1\" is too large to use as a pattern (over 30,000 pixels wide or tall).")
                .arg(file_name);
    return {};
  }
  if (exceeds_pixel_cap(image.size())) {
    error = tr("\"%1\" is too large to use as a pattern (over 8 million pixels).").arg(file_name);
    return {};
  }
  if (reader.supportsAnimation() && reader.imageCount() > 1) {
    warnings.append(tr("Animated image \"%1\": imported the first frame only.").arg(file_name));
  }
  const auto tile = pattern_tile_from_image(image);
  if (!tile.has_value()) {
    error = tr("Could not read \"%1\" as an image.").arg(file_name);
    return {};
  }
  const auto name = QFileInfo(path).completeBaseName();
  const auto storage_id = add_pattern_internal(name, *tile, {}, {});
  if (storage_id.isEmpty()) {
    error = tr("Could not save pattern \"%1\".").arg(name);
    return {};
  }
  sort_entries();
  emit changed();
  return storage_id;
}

QString PatternLibrary::duplicate_pattern(const QString& storage_id) {
  const auto* entry = find_entry(storage_id);
  if (entry == nullptr) {
    return {};
  }
  const auto original = resource_for_entry(entry->storage_id);
  if (!original.has_value()) {
    return {};
  }
  const auto copy_name = tr("%1 Copy").arg(pattern_library_entry_display_name(*entry));
  const auto duplicate_id = add_pattern_internal(copy_name, original->tile, entry->folder, {});
  if (!duplicate_id.isEmpty()) {
    sort_entries();
    emit changed();
  }
  return duplicate_id;
}

bool PatternLibrary::rename_pattern(const QString& storage_id, const QString& name) {
  const auto trimmed = name.trimmed();
  const auto found = std::find_if(entries_.begin(), entries_.end(),
                                  [&storage_id](const PatternLibraryEntry& entry) {
                                    return entry.storage_id == storage_id;
                                  });
  if (found == entries_.end() || trimmed.isEmpty()) {
    return false;
  }
  if (found->name == trimmed) {
    return true;
  }
  auto updated = *found;
  updated.name = trimmed;
  if (!write_sidecar(updated)) {
    return false;
  }
  found->name = trimmed;
  sort_entries();
  emit changed();
  return true;
}

bool PatternLibrary::set_pattern_folder(const QString& storage_id, const QString& folder) {
  const auto found = std::find_if(entries_.begin(), entries_.end(),
                                  [&storage_id](const PatternLibraryEntry& entry) {
                                    return entry.storage_id == storage_id;
                                  });
  if (found == entries_.end()) {
    return false;
  }
  const auto trimmed = folder.trimmed();
  if (found->folder == trimmed) {
    return true;
  }
  auto updated = *found;
  updated.folder = trimmed;
  if (!write_sidecar(updated)) {
    return false;
  }
  found->folder = trimmed;
  sort_entries();
  emit changed();
  return true;
}

bool PatternLibrary::remove_pattern_internal(const QString& storage_id) {
  const auto found = std::find_if(entries_.begin(), entries_.end(),
                                  [&storage_id](const PatternLibraryEntry& entry) {
                                    return entry.storage_id == storage_id;
                                  });
  if (found == entries_.end()) {
    return false;
  }
  const auto tile_path = png_path(storage_id);
  if (QFileInfo::exists(tile_path) && !QFile::remove(tile_path) && QFileInfo::exists(tile_path)) {
    return false;
  }
  (void)QFile::remove(json_path(storage_id));
  invalidate_cached_tile(storage_id);
  entries_.erase(found);
  return true;
}

bool PatternLibrary::remove_pattern(const QString& storage_id) {
  if (!remove_pattern_internal(storage_id)) {
    return false;
  }
  emit changed();
  return true;
}

int PatternLibrary::remove_patterns(const QStringList& storage_ids) {
  int removed = 0;
  for (const auto& storage_id : storage_ids) {
    if (remove_pattern_internal(storage_id)) {
      ++removed;
    }
  }
  if (removed > 0) {
    emit changed();
  }
  return removed;
}

namespace {

// One row of the factory roster: the code-generated presets (library version 1)
// plus the bundled photo textures (introduced_version per entry). Tiles resolve
// lazily so has-all checks stay cheap.
struct FactoryPatternEntry {
  QString id;
  QString english_name;
  QString folder;
  int introduced_version;
  std::function<PixelBuffer()> tile;
};

[[nodiscard]] std::vector<FactoryPatternEntry> factory_pattern_roster() {
  std::vector<FactoryPatternEntry> roster;
  const auto generated_folder = default_patterns_folder_name();
  for (const auto& preset : builtin_pattern_presets()) {
    roster.push_back(FactoryPatternEntry{
        QString::fromLatin1(preset.id), QString::fromLatin1(preset.english_name),
        generated_folder, kInitialBuiltinPatternVersion,
        [id = preset.id] { return builtin_pattern_resource(id).tile; }});
  }
  const auto photo_folder = photo_patterns_folder_name();
  for (const auto& preset : photo_pattern_presets()) {
    roster.push_back(FactoryPatternEntry{
        QString::fromLatin1(preset.id), QString::fromLatin1(preset.english_name), photo_folder,
        preset.introduced_version, [id = preset.id] {
          const auto resource = photo_pattern_resource(id);
          return resource.has_value() ? resource->tile : PixelBuffer{};
        }});
  }
  return roster;
}

}  // namespace

int PatternLibrary::restore_default_patterns(int newer_than_version) {
  int restored = 0;
  for (const auto& preset : factory_pattern_roster()) {
    if (preset.introduced_version <= newer_than_version) {
      continue;
    }
    if (find_entry_by_pattern_id(preset.id) != nullptr) {
      continue;
    }
    const auto tile = preset.tile();
    if (!tile.empty() &&
        !add_pattern_internal(preset.english_name, tile, preset.folder, preset.id).isEmpty()) {
      ++restored;
    }
  }
  if (restored > 0) {
    sort_entries();
    emit changed();
  }
  return restored;
}

bool PatternLibrary::has_all_default_patterns_introduced_after(
    int newer_than_version) const {
  const auto roster = factory_pattern_roster();
  return std::all_of(roster.begin(), roster.end(), [&](const FactoryPatternEntry& preset) {
    return preset.introduced_version <= newer_than_version ||
           find_entry_by_pattern_id(preset.id) != nullptr;
  });
}

bool PatternLibrary::default_patterns_match_factory() const {
  for (const auto& preset : factory_pattern_roster()) {
    const auto* entry = find_entry_by_pattern_id(preset.id);
    if (entry == nullptr || !entry->source_id.isEmpty() || entry->name != preset.english_name ||
        entry->folder != preset.folder) {
      return false;
    }
    const auto current = tile_for_entry(*entry);
    const auto expected = preset.tile();
    if (!current.has_value() || expected.empty() || !pattern_tiles_equal(*current, expected)) {
      return false;
    }
  }
  return true;
}

int PatternLibrary::reset_default_patterns_to_factory() {
  int reset = 0;
  bool any_changed = false;
  for (const auto& preset : factory_pattern_roster()) {
    const auto pattern_id = preset.id;
    const auto folder = preset.folder;
    const auto found = std::find_if(entries_.begin(), entries_.end(),
                                    [&pattern_id](const PatternLibraryEntry& entry) {
                                      return entry.id == pattern_id;
                                    });
    if (found == entries_.end()) {
      continue;  // restore_default_patterns() owns missing entries
    }
    PatternResource expected;
    expected.tile = preset.tile();
    if (expected.tile.empty()) {
      continue;
    }
    const auto current_tile = tile_for_entry(*found);
    const auto tile_changed =
        !current_tile.has_value() || !pattern_tiles_equal(*current_tile, expected.tile);
    const auto& canonical_name = preset.english_name;
    const auto metadata_changed = found->name != canonical_name || found->folder != folder ||
                                  !found->source_id.isEmpty();
    if (!tile_changed && !metadata_changed) {
      continue;
    }

    auto tile_saved = !tile_changed;
    auto metadata_saved = !metadata_changed;
    if (tile_changed) {
      const auto expected_image = image_from_pattern_tile(expected.tile);
      if (save_png(expected_image, png_path(found->storage_id))) {
        found->size = QSize(expected.tile.width(), expected.tile.height());
        found->thumbnail = pattern_thumbnail(expected.tile);
        cache_tile(found->storage_id, expected.tile);
        tile_saved = true;
        any_changed = true;
      }
    }
    if (metadata_changed) {
      auto updated = *found;
      updated.name = canonical_name;
      updated.folder = folder;
      updated.source_id.clear();
      if (write_sidecar(updated)) {
        found->name = canonical_name;
        found->folder = folder;
        found->source_id.clear();
        metadata_saved = true;
        any_changed = true;
      }
    }
    if (tile_saved && metadata_saved) {
      ++reset;
    }
  }
  if (any_changed) {
    sort_entries();
    emit changed();
  }
  return reset;
}

}  // namespace patchy::ui
