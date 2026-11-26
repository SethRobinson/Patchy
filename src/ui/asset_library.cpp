#include "ui/asset_library.hpp"

#include <QBuffer>
#include <QCollator>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeData>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QUuid>

#include <algorithm>
#include <utility>

namespace patchy::ui {

namespace {

// The index file's own tokens: permanent (a file format).
constexpr auto kIndexFileName = "index.json";
constexpr auto kIndexFormat = "patchy-assets";
constexpr int kIndexVersion = 1;
constexpr int kMaximumNameLength = 120;
// Far above any real library; bounds what a damaged index can make the panel build.
constexpr int kMaximumIndexBytes = 16 * 1024 * 1024;

// A PSD or PSB starts with this signature: enough to leave a foreign or truncated file out
// without decoding every asset when the panel opens.
bool file_has_psd_signature(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    return false;
  }
  return file.read(4) == QByteArrayLiteral("8BPS");
}

bool write_file_atomically(const QString& path, const QByteArray& bytes, QString* error) {
  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
    if (error != nullptr) {
      *error = QCoreApplication::translate("patchy::ui::AssetLibrary", "Could not write %1: %2")
                   .arg(QDir::toNativeSeparators(path), file.errorString());
    }
    return false;
  }
  return true;
}

QString new_id() { return QUuid::createUuid().toString(QUuid::Id128); }

}  // namespace

QString asset_library_directory() {
#ifdef Q_OS_WASM
  return {};
#else
  // Isolation knob, like PATCHY_USER_FONTS_DIR: the test suites never read or write the
  // developer's own library.
  if (const auto override_dir = qEnvironmentVariable("PATCHY_ASSETS_DIR"); !override_dir.isEmpty()) {
    return QDir::cleanPath(override_dir);
  }
  const auto base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  if (base.isEmpty()) {
    return {};
  }
  return base + QStringLiteral("/assets");
#endif
}

QStringList asset_ids_from_mime_data(const QMimeData* mime_data) {
  QStringList ids;
  if (mime_data == nullptr || !mime_data->hasFormat(QLatin1String(kAssetDragMimeType))) {
    return ids;
  }
  const auto lines = QString::fromUtf8(mime_data->data(QLatin1String(kAssetDragMimeType))).split(QLatin1Char('\n'));
  for (const auto& line : lines) {
    if (AssetLibrary::is_asset_id(line) && !ids.contains(line)) {
      ids.push_back(line);
    }
  }
  return ids;
}

bool asset_drag_is_from_this_process(const QMimeData* mime_data) {
  if (mime_data == nullptr || !mime_data->hasFormat(QLatin1String(kAssetDragSourceMimeType))) {
    return false;
  }
  return mime_data->data(QLatin1String(kAssetDragSourceMimeType)) ==
         QByteArray::number(QCoreApplication::applicationPid());
}

AssetLibrary::AssetLibrary(QString directory, QObject* parent) : QObject(parent), directory_(std::move(directory)) {
  ensure_default_category();
}

void AssetLibrary::set_recovered_name_provider(std::function<QString(const QString& asset_path)> provider) {
  recovered_name_provider_ = std::move(provider);
}

bool AssetLibrary::is_asset_id(const QString& text) {
  if (text.size() != 32) {
    return false;
  }
  return std::all_of(text.begin(), text.end(), [](QChar character) {
    const auto code = character.unicode();
    return (code >= u'0' && code <= u'9') || (code >= u'a' && code <= u'f');
  });
}

QString AssetLibrary::cleaned_name(const QString& name) {
  auto cleaned = name;
  cleaned.replace(QLatin1Char('\n'), QLatin1Char(' '));
  cleaned.replace(QLatin1Char('\r'), QLatin1Char(' '));
  cleaned.replace(QLatin1Char('\t'), QLatin1Char(' '));
  cleaned = cleaned.simplified();
  if (cleaned.size() > kMaximumNameLength) {
    cleaned.truncate(kMaximumNameLength);
    cleaned = cleaned.trimmed();
  }
  return cleaned;
}

QString AssetLibrary::asset_path(const QString& asset_id) const {
  if (directory_.isEmpty() || !is_asset_id(asset_id)) {
    return {};
  }
  return QDir(directory_).filePath(asset_id + QStringLiteral(".psd"));
}

QString AssetLibrary::thumbnail_path(const QString& asset_id) const {
  if (directory_.isEmpty() || !is_asset_id(asset_id)) {
    return {};
  }
  return QDir(directory_).filePath(asset_id + QStringLiteral(".png"));
}

QString AssetLibrary::index_path() const {
  return directory_.isEmpty() ? QString() : QDir(directory_).filePath(QLatin1String(kIndexFileName));
}

void AssetLibrary::ensure_default_category() {
  if (categories_.empty()) {
    categories_.push_back(AssetCategory{QLatin1String(kDefaultAssetCategoryId), QString(), {}});
  }
}

void AssetLibrary::ensure_loaded() {
  if (!loaded_) {
    load();
  }
}

void AssetLibrary::load() {
  loaded_ = true;
  categories_.clear();
  const auto path = index_path();
  bool index_read = false;
  if (!path.isEmpty()) {
    QFile file(path);
    if (file.open(QIODevice::ReadOnly) && file.size() <= kMaximumIndexBytes) {
      QJsonParseError parse_error;
      const auto json = QJsonDocument::fromJson(file.readAll(), &parse_error);
      const auto root = json.object();
      if (parse_error.error == QJsonParseError::NoError && json.isObject() &&
          root.value(QStringLiteral("format")).toString() == QLatin1String(kIndexFormat) &&
          root.value(QStringLiteral("categories")).isArray()) {
        index_read = true;
        QSet<QString> seen_categories;
        QSet<QString> seen_assets;
        const auto categories = root.value(QStringLiteral("categories")).toArray();
        for (const auto& category_value : categories) {
          const auto category_object = category_value.toObject();
          AssetCategory category;
          category.id = category_object.value(QStringLiteral("id")).toString();
          if (category.id.isEmpty() || seen_categories.contains(category.id)) {
            category.id = new_id();
          }
          seen_categories.insert(category.id);
          category.name = cleaned_name(category_object.value(QStringLiteral("name")).toString());
          const auto assets = category_object.value(QStringLiteral("assets")).toArray();
          for (const auto& asset_value : assets) {
            const auto asset_object = asset_value.toObject();
            AssetEntry entry;
            entry.id = asset_object.value(QStringLiteral("id")).toString();
            // An entry whose file is gone, is not a PSD or is listed twice is left out:
            // the rest of the library still loads.
            if (!is_asset_id(entry.id) || seen_assets.contains(entry.id) ||
                !file_has_psd_signature(asset_path(entry.id))) {
              continue;
            }
            seen_assets.insert(entry.id);
            entry.name = cleaned_name(asset_object.value(QStringLiteral("name")).toString());
            category.assets.push_back(std::move(entry));
          }
          categories_.push_back(std::move(category));
        }
      }
    }
  }
  if (!index_read) {
    rebuild_from_files();
  }
  ensure_default_category();
  emit changed();
}

void AssetLibrary::rebuild_from_files() {
  // No usable index: every asset file still in the folder comes back in the first
  // category, oldest first, so a lost index loses names and categories but no asset.
  categories_.clear();
  ensure_default_category();
  if (directory_.isEmpty()) {
    return;
  }
  const QDir folder(directory_);
  if (!folder.exists()) {
    return;
  }
  const auto files = folder.entryInfoList({QStringLiteral("*.psd")}, QDir::Files, QDir::Time | QDir::Reversed);
  for (const auto& info : files) {
    const auto id = info.completeBaseName();
    if (!is_asset_id(id) || !file_has_psd_signature(info.absoluteFilePath())) {
      continue;
    }
    QString name;
    if (recovered_name_provider_) {
      name = cleaned_name(recovered_name_provider_(info.absoluteFilePath()));
    }
    categories_.front().assets.push_back(AssetEntry{id, name});
  }
}

const AssetCategory* AssetLibrary::find_category(const QString& category_id) const {
  const auto it = std::find_if(categories_.begin(), categories_.end(),
                               [&](const AssetCategory& entry) { return entry.id == category_id; });
  return it != categories_.end() ? &*it : nullptr;
}

AssetCategory* AssetLibrary::category(const QString& category_id) {
  const auto it = std::find_if(categories_.begin(), categories_.end(),
                               [&](const AssetCategory& entry) { return entry.id == category_id; });
  return it != categories_.end() ? &*it : nullptr;
}

const AssetEntry* AssetLibrary::find_asset(const QString& asset_id) const {
  for (const auto& entry : categories_) {
    const auto it = std::find_if(entry.assets.begin(), entry.assets.end(),
                                 [&](const AssetEntry& asset) { return asset.id == asset_id; });
    if (it != entry.assets.end()) {
      return &*it;
    }
  }
  return nullptr;
}

QString AssetLibrary::category_of(const QString& asset_id) const {
  for (const auto& entry : categories_) {
    if (std::any_of(entry.assets.begin(), entry.assets.end(),
                    [&](const AssetEntry& asset) { return asset.id == asset_id; })) {
      return entry.id;
    }
  }
  return {};
}

int AssetLibrary::asset_count() const {
  int count = 0;
  for (const auto& entry : categories_) {
    count += static_cast<int>(entry.assets.size());
  }
  return count;
}

bool AssetLibrary::save_index(QString* error) {
  if (directory_.isEmpty()) {
    if (error != nullptr) {
      *error = tr("There is no folder to keep assets in");
    }
    return false;
  }
  if (!QDir().mkpath(directory_)) {
    if (error != nullptr) {
      *error = tr("Could not create %1").arg(QDir::toNativeSeparators(directory_));
    }
    return false;
  }
  QJsonArray categories;
  for (const auto& entry : categories_) {
    QJsonArray assets;
    for (const auto& asset : entry.assets) {
      assets.push_back(QJsonObject{{QStringLiteral("id"), asset.id}, {QStringLiteral("name"), asset.name}});
    }
    categories.push_back(QJsonObject{{QStringLiteral("id"), entry.id},
                                     {QStringLiteral("name"), entry.name},
                                     {QStringLiteral("assets"), assets}});
  }
  const QJsonObject root{{QStringLiteral("format"), QLatin1String(kIndexFormat)},
                         {QStringLiteral("version"), kIndexVersion},
                         {QStringLiteral("categories"), categories}};
  return write_file_atomically(index_path(), QJsonDocument(root).toJson(QJsonDocument::Indented), error);
}

std::optional<QString> AssetLibrary::add_asset(const QString& category_id, const QString& name,
                                               std::span<const std::uint8_t> psd_bytes, const QImage& thumbnail,
                                               QString* error) {
  ensure_loaded();
  auto* target = category(category_id);
  if (target == nullptr) {
    target = &categories_.front();
  }
  if (directory_.isEmpty() || psd_bytes.empty()) {
    if (error != nullptr) {
      *error = tr("There is no folder to keep assets in");
    }
    return std::nullopt;
  }
  if (!QDir().mkpath(directory_)) {
    if (error != nullptr) {
      *error = tr("Could not create %1").arg(QDir::toNativeSeparators(directory_));
    }
    return std::nullopt;
  }
  // File names are generated ids, never the user's name for the asset.
  auto id = new_id();
  while (QFileInfo::exists(asset_path(id))) {
    id = new_id();
  }
  const auto file_path = asset_path(id);
  const QByteArray bytes(reinterpret_cast<const char*>(psd_bytes.data()), static_cast<qsizetype>(psd_bytes.size()));
  if (!write_file_atomically(file_path, bytes, error)) {
    return std::nullopt;
  }
  if (!thumbnail.isNull()) {
    store_thumbnail(id, thumbnail);  // a cache: a failed write is made again when it is next needed
  }
  const auto before = categories_;
  target->assets.push_back(AssetEntry{id, cleaned_name(name)});
  if (!save_index(error)) {
    categories_ = before;
    QFile::remove(file_path);
    QFile::remove(thumbnail_path(id));
    return std::nullopt;
  }
  emit changed();
  return id;
}

bool AssetLibrary::rename_asset(const QString& asset_id, const QString& name, QString* error) {
  ensure_loaded();
  const auto before = categories_;
  for (auto& entry : categories_) {
    for (auto& asset : entry.assets) {
      if (asset.id != asset_id) {
        continue;
      }
      asset.name = cleaned_name(name);
      if (!save_index(error)) {
        categories_ = before;
        return false;
      }
      emit changed();
      return true;
    }
  }
  return false;
}

bool AssetLibrary::remove_asset(const QString& asset_id, QString* error) {
  ensure_loaded();
  const auto before = categories_;
  for (auto& entry : categories_) {
    const auto it = std::find_if(entry.assets.begin(), entry.assets.end(),
                                 [&](const AssetEntry& asset) { return asset.id == asset_id; });
    if (it == entry.assets.end()) {
      continue;
    }
    entry.assets.erase(it);
    // The index first: a stop between the two steps leaves a file nothing lists, never
    // an entry without its file.
    if (!save_index(error)) {
      categories_ = before;
      return false;
    }
    QFile::remove(asset_path(asset_id));
    QFile::remove(thumbnail_path(asset_id));
    emit changed();
    return true;
  }
  return false;
}

bool AssetLibrary::move_asset(const QString& asset_id, const QString& category_id, QString* error) {
  ensure_loaded();
  if (category(category_id) == nullptr) {
    return false;
  }
  const auto before = categories_;
  for (auto& entry : categories_) {
    const auto it = std::find_if(entry.assets.begin(), entry.assets.end(),
                                 [&](const AssetEntry& asset) { return asset.id == asset_id; });
    if (it == entry.assets.end()) {
      continue;
    }
    if (entry.id == category_id) {
      return true;
    }
    auto moved = std::move(*it);
    entry.assets.erase(it);
    category(category_id)->assets.push_back(std::move(moved));
    if (!save_index(error)) {
      categories_ = before;
      return false;
    }
    emit changed();
    return true;
  }
  return false;
}

std::optional<QString> AssetLibrary::add_category(const QString& name, QString* error) {
  ensure_loaded();
  const auto cleaned = cleaned_name(name);
  if (cleaned.isEmpty()) {
    return std::nullopt;
  }
  const auto before = categories_;
  auto id = new_id();
  categories_.push_back(AssetCategory{id, cleaned, {}});
  if (!save_index(error)) {
    categories_ = before;
    return std::nullopt;
  }
  emit changed();
  return id;
}

bool AssetLibrary::rename_category(const QString& category_id, const QString& name, QString* error) {
  ensure_loaded();
  const auto cleaned = cleaned_name(name);
  auto* target = category(category_id);
  if (target == nullptr || cleaned.isEmpty()) {
    return false;
  }
  const auto before = categories_;
  target->name = cleaned;
  if (!save_index(error)) {
    categories_ = before;
    return false;
  }
  emit changed();
  return true;
}

bool AssetLibrary::remove_category(const QString& category_id, QString* error) {
  ensure_loaded();
  const auto it = std::find_if(categories_.begin(), categories_.end(),
                               [&](const AssetCategory& entry) { return entry.id == category_id; });
  if (it == categories_.end() || categories_.size() <= 1U) {
    return false;
  }
  const auto before = categories_;
  const auto removed = it->assets;
  categories_.erase(it);
  if (!save_index(error)) {
    categories_ = before;
    return false;
  }
  for (const auto& asset : removed) {
    QFile::remove(asset_path(asset.id));
    QFile::remove(thumbnail_path(asset.id));
  }
  emit changed();
  return true;
}

bool AssetLibrary::sort_category_by_name(const QString& category_id, QString* error) {
  ensure_loaded();
  auto* target = category(category_id);
  if (target == nullptr) {
    return false;
  }
  const auto before = categories_;
  QCollator collator;
  collator.setCaseSensitivity(Qt::CaseInsensitive);
  collator.setNumericMode(true);
  std::stable_sort(target->assets.begin(), target->assets.end(), [&collator](const AssetEntry& left, const AssetEntry& right) {
    return collator.compare(left.name, right.name) < 0;
  });
  if (!save_index(error)) {
    categories_ = before;
    return false;
  }
  emit changed();
  return true;
}

QImage AssetLibrary::thumbnail(const QString& asset_id) const {
  const auto path = thumbnail_path(asset_id);
  if (path.isEmpty() || !QFileInfo::exists(path)) {
    return {};
  }
  QImage image;
  image.load(path, "PNG");
  return image;
}

bool AssetLibrary::store_thumbnail(const QString& asset_id, const QImage& thumbnail) {
  const auto path = thumbnail_path(asset_id);
  if (path.isEmpty() || thumbnail.isNull() || !QDir().mkpath(directory_)) {
    return false;
  }
  QByteArray bytes;
  QBuffer buffer(&bytes);
  if (!buffer.open(QIODevice::WriteOnly) || !thumbnail.save(&buffer, "PNG")) {
    return false;
  }
  return write_file_atomically(path, bytes, nullptr);
}

QByteArray AssetLibrary::read_asset_bytes(const QString& asset_id) const {
  const auto path = asset_path(asset_id);
  if (path.isEmpty()) {
    return {};
  }
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    return {};
  }
  return file.readAll();
}

}  // namespace patchy::ui
