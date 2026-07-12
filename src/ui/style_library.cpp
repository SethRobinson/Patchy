#include "ui/style_library.hpp"

#include "core/document.hpp"
#include "core/pattern_presets.hpp"
#include "core/style_presets.hpp"
#include "psd/psd_layer_effects.hpp"
#include "render/compositor.hpp"
#include "ui/app_settings.hpp"
#include "ui/photo_pattern_presets.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QSaveFile>
#include <QUuid>

#include <algorithm>
#include <cstring>
#include <exception>
#include <string>
#include <utility>

namespace patchy::ui {

namespace {

constexpr std::size_t kPatternCacheLimit = 8;
constexpr qint64 kMaxAslFileBytes = 32LL * 1024LL * 1024LL;

[[nodiscard]] QString default_storage_dir() {
  const auto settings = app_settings();
  return QFileInfo(settings.fileName()).absolutePath() + QStringLiteral("/styles");
}

[[nodiscard]] QString qstring_from_utf8(const std::string& text) {
  return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

[[nodiscard]] std::string utf8_from_qstring(const QString& text) {
  const auto bytes = text.toUtf8();
  return std::string(bytes.constData(), static_cast<std::size_t>(bytes.size()));
}

[[nodiscard]] QString fresh_style_id() {
  return qstring_from_utf8(generate_pattern_uuid());
}

// Canonical comparison for "same recipe": the regenerated lfx2 payload covers
// every modeled effect field (and referenced pattern ids) deterministically.
[[nodiscard]] bool styles_equal(const LayerStyle& lhs, const LayerStyle& rhs) {
  return psd::photoshop_lfx2_layer_style_payload(lhs) ==
         psd::photoshop_lfx2_layer_style_payload(rhs);
}

[[nodiscard]] bool pattern_tiles_equal(const PixelBuffer& lhs, const PixelBuffer& rhs) {
  return lhs.width() == rhs.width() && lhs.height() == rhs.height() &&
         lhs.format() == rhs.format() && lhs.data().size() == rhs.data().size() &&
         std::equal(lhs.data().begin(), lhs.data().end(), rhs.data().begin());
}

[[nodiscard]] std::optional<PixelBuffer> rgba_buffer_from_image(const QImage& source) {
  if (source.isNull() || source.width() <= 0 || source.height() <= 0) {
    return std::nullopt;
  }
  const auto image = source.convertToFormat(QImage::Format_RGBA8888);
  if (image.isNull()) {
    return std::nullopt;
  }
  try {
    PixelBuffer buffer(image.width(), image.height(), PixelFormat::rgba8());
    const auto row_bytes = static_cast<std::size_t>(image.width()) * 4U;
    for (int y = 0; y < image.height(); ++y) {
      std::memcpy(buffer.row(y).data(), image.constScanLine(y), row_bytes);
    }
    return buffer;
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

// The pattern tiles a style actually references, in reference order.
[[nodiscard]] std::vector<PatternResource> referenced_patterns(
    const LayerStyle& style, std::span<const PatternResource> available) {
  std::vector<std::string> ids;
  collect_referenced_pattern_ids(style, ids);
  std::vector<PatternResource> result;
  for (const auto& id : ids) {
    const auto found = std::find_if(available.begin(), available.end(),
                                    [&id](const PatternResource& pattern) {
                                      return pattern.id == id;
                                    });
    if (found != available.end()) {
      result.push_back(*found);
    } else if (auto bundled = bundled_pattern_resource(id); bundled.has_value()) {
      result.push_back(std::move(*bundled));
    }
  }
  return result;
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

}  // namespace

QString style_preset_folder_display_name(const char* english_folder) {
  // Keep the QObject translation context shared with the other preset names.
  return QCoreApplication::translate("QObject", english_folder);
}

QString style_library_entry_display_name(const StyleLibraryEntry& entry) {
  const auto id = utf8_from_qstring(entry.id);
  if (const auto* preset = find_builtin_style_preset(id); preset != nullptr) {
    const auto canonical = QString::fromLatin1(preset->english_name);
    if (entry.name == canonical) {
      return QCoreApplication::translate("QObject", preset->english_name);
    }
  }
  return entry.name;
}

QPixmap render_style_preview(const LayerStyle& style,
                             const std::optional<psd::AslBlendSettings>& blend_settings,
                             std::span<const PatternResource> patterns, int extent) {
  // Rendered at the requested extent (the manager's large preview stays crisp);
  // effect sizes are document pixels, so bigger cards show more surrounding
  // room rather than scaled-up effects.
  const int render_size = std::max(24, extent);

  // Neutral mid-gray card: dark shadows and screen-blend glows both stay
  // visible, and dark-neutral sample glyphs read under face overlays.
  QImage card_image(render_size, render_size, QImage::Format_RGBA8888);
  {
    QPainter painter(&card_image);
    QLinearGradient gradient(0.0, 0.0, 0.0, render_size);
    gradient.setColorAt(0.0, QColor(185, 190, 198));
    gradient.setColorAt(1.0, QColor(135, 141, 150));
    painter.fillRect(card_image.rect(), gradient);
  }

  QImage sample(render_size, render_size, QImage::Format_ARGB32_Premultiplied);
  sample.fill(Qt::transparent);
  {
    QPainter painter(&sample);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    auto font = QGuiApplication::font();
    font.setBold(true);
    font.setPixelSize(render_size * 11 / 20);
    painter.setFont(font);
    painter.setPen(QColor(58, 62, 69));
    painter.drawText(sample.rect(), Qt::AlignCenter, QStringLiteral("Aa"));
  }
  auto sample_buffer = rgba_buffer_from_image(sample);
  auto has_ink = false;
  if (sample_buffer.has_value()) {
    for (std::int32_t y = 0; !has_ink && y < sample_buffer->height(); ++y) {
      const auto row = sample_buffer->row(y);
      for (std::int32_t x = 3; x < static_cast<std::int32_t>(row.size()); x += 4) {
        if (row[static_cast<std::size_t>(x)] > 16) {
          has_ink = true;
          break;
        }
      }
    }
  }
  if (!has_ink) {
    // No usable font glyphs (offscreen test platforms): a rounded square keeps
    // thumbnails meaningful and deterministic.
    sample.fill(Qt::transparent);
    QPainter painter(&sample);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QPainterPath path;
    const auto inset = render_size * 30 / 100;
    path.addRoundedRect(QRectF(inset, inset, render_size - 2 * inset, render_size - 2 * inset),
                        6.0, 6.0);
    painter.fillPath(path, QColor(58, 62, 69));
    painter.end();
    sample_buffer = rgba_buffer_from_image(sample);
  }

  const auto card_buffer = rgba_buffer_from_image(card_image);
  if (!card_buffer.has_value() || !sample_buffer.has_value()) {
    return {};
  }

  Document document(render_size, render_size, PixelFormat::rgba8());
  document.add_layer(Layer(1, "card", *card_buffer));
  auto& sample_layer = document.add_layer(Layer(2, "sample", *sample_buffer));
  sample_layer.layer_style() = style;
  if (blend_settings.has_value()) {
    sample_layer.set_opacity(static_cast<float>(blend_settings->opacity) / 100.0F);
    sample_layer.set_blend_mode(blend_settings->blend_mode);
    (void)sample_layer.set_blend_if(blend_settings->blend_if);
  }
  for (const auto& pattern : patterns) {
    document.metadata().patterns.adopt(pattern);
  }
  std::vector<std::string> referenced_ids;
  collect_referenced_pattern_ids(style, referenced_ids);
  for (const auto& id : referenced_ids) {
    if (document.metadata().patterns.find(id) != nullptr) {
      continue;
    }
    if (auto bundled = bundled_pattern_resource(id); bundled.has_value()) {
      document.metadata().patterns.adopt(*bundled);
    }
  }

  QImage rendered;
  try {
    const auto flattened = Compositor().flatten_rgb8(document);
    if (flattened.empty() || flattened.width() <= 0 || flattened.height() <= 0) {
      return {};
    }
    rendered = QImage(flattened.width(), flattened.height(), QImage::Format_RGB888);
    const auto row_bytes = static_cast<std::size_t>(flattened.width()) * 3U;
    for (std::int32_t y = 0; y < flattened.height(); ++y) {
      std::memcpy(rendered.scanLine(y), flattened.row(y).data(), row_bytes);
    }
  } catch (const std::exception&) {
    return {};
  }

  {
    QPainter painter(&rendered);
    painter.setPen(QColor(0, 0, 0, 90));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(0, 0, rendered.width() - 1, rendered.height() - 1);
  }
  if (extent != render_size) {
    rendered = rendered.scaled(extent, extent, Qt::KeepAspectRatio, Qt::SmoothTransformation);
  }
  return QPixmap::fromImage(rendered);
}

StyleLibrary::StyleLibrary(QString storage_dir, QObject* parent)
    : QObject(parent),
      storage_dir_(storage_dir.isEmpty() ? default_storage_dir() : std::move(storage_dir)) {
  reload();
}

const QString& StyleLibrary::storage_dir() const noexcept {
  return storage_dir_;
}

const std::vector<StyleLibraryEntry>& StyleLibrary::entries() const noexcept {
  return entries_;
}

const StyleLibraryEntry* StyleLibrary::find_entry(const QString& storage_id) const {
  const auto found = std::find_if(entries_.begin(), entries_.end(),
                                  [&storage_id](const StyleLibraryEntry& entry) {
                                    return entry.storage_id == storage_id;
                                  });
  return found == entries_.end() ? nullptr : &*found;
}

const StyleLibraryEntry* StyleLibrary::find_entry_by_style_id(const QString& id) const {
  const auto found = std::find_if(entries_.begin(), entries_.end(),
                                  [&id](const StyleLibraryEntry& entry) {
                                    return entry.id == id;
                                  });
  return found == entries_.end() ? nullptr : &*found;
}

QString StyleLibrary::asl_path(const QString& storage_id) const {
  return storage_dir_ + QStringLiteral("/") + storage_id + QStringLiteral(".asl");
}

QString StyleLibrary::json_path(const QString& storage_id) const {
  return storage_dir_ + QStringLiteral("/") + storage_id + QStringLiteral(".json");
}

QString StyleLibrary::thumbnail_path(const QString& storage_id) const {
  return storage_dir_ + QStringLiteral("/") + storage_id + QStringLiteral(".png");
}

std::optional<psd::AslReadResult> StyleLibrary::read_entry_asl(const QString& storage_id) const {
  QFile file(asl_path(storage_id));
  if (!file.open(QIODevice::ReadOnly) || file.size() > kMaxAslFileBytes) {
    return std::nullopt;
  }
  const auto bytes = file.readAll();
  std::string error;
  return psd::read_asl(
      std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(bytes.constData()),
                                    static_cast<std::size_t>(bytes.size())),
      error);
}

std::vector<PatternResource> StyleLibrary::patterns_for_entry(const QString& storage_id) const {
  for (const auto& cached : pattern_cache_) {
    if (cached.first == storage_id) {
      return cached.second;
    }
  }
  if (find_entry(storage_id) == nullptr) {
    return {};
  }
  auto parsed = read_entry_asl(storage_id);
  std::vector<PatternResource> patterns;
  if (parsed.has_value()) {
    patterns = std::move(parsed->patterns);
  }
  if (pattern_cache_.size() >= kPatternCacheLimit) {
    pattern_cache_.erase(pattern_cache_.begin());
  }
  pattern_cache_.emplace_back(storage_id, patterns);
  return patterns;
}

void StyleLibrary::invalidate_cached_patterns(const QString& storage_id) const {
  pattern_cache_.erase(std::remove_if(pattern_cache_.begin(), pattern_cache_.end(),
                                      [&storage_id](const auto& cached) {
                                        return cached.first == storage_id;
                                      }),
                       pattern_cache_.end());
}

void StyleLibrary::refresh_thumbnail(StyleLibraryEntry& entry,
                                     std::span<const PatternResource> patterns, bool save_cache) {
  entry.thumbnail =
      render_style_preview(entry.style, entry.blend_settings, patterns, kStyleThumbnailExtent);
  if (save_cache && !entry.thumbnail.isNull()) {
    (void)save_png(entry.thumbnail.toImage(), thumbnail_path(entry.storage_id));
  }
}

void StyleLibrary::reload() {
  entries_.clear();
  pattern_cache_.clear();
  const QDir dir(storage_dir_);
  if (!dir.exists()) {
    return;
  }

  const auto asl_files = dir.entryInfoList({QStringLiteral("*.asl")}, QDir::Files, QDir::Name);
  for (const auto& file_info : asl_files) {
    const auto storage_id = file_info.completeBaseName();
    auto parsed = read_entry_asl(storage_id);
    if (!parsed.has_value() || parsed->styles.empty()) {
      continue;  // one corrupt entry never hides the rest of the library
    }
    auto& style = parsed->styles.front();

    StyleLibraryEntry entry;
    entry.storage_id = storage_id;
    entry.id = qstring_from_utf8(style.id);
    entry.name = qstring_from_utf8(style.name);
    entry.style = std::move(style.style);
    entry.blend_settings = style.blend_settings;
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
    if (entry.id.isEmpty() || find_entry_by_style_id(entry.id) != nullptr) {
      continue;  // ids are unique within the installed library; first file wins
    }
    if (entry.name.trimmed().isEmpty()) {
      entry.name = tr("Untitled Style");
    }

    const QFileInfo thumbnail_info(thumbnail_path(storage_id));
    if (thumbnail_info.exists() &&
        thumbnail_info.lastModified() >= file_info.lastModified()) {
      entry.thumbnail = QPixmap(thumbnail_info.absoluteFilePath());
    }
    if (entry.thumbnail.isNull()) {
      refresh_thumbnail(entry, parsed->patterns, true);
    }
    entries_.push_back(std::move(entry));
  }
  sort_entries();
}

void StyleLibrary::sort_entries() {
  std::sort(entries_.begin(), entries_.end(),
            [](const StyleLibraryEntry& a, const StyleLibraryEntry& b) {
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

QStringList StyleLibrary::folders() const {
  QStringList result;
  for (const auto& entry : entries_) {
    if (!entry.folder.isEmpty() && !result.contains(entry.folder)) {
      result.append(entry.folder);
    }
  }
  return result;
}

bool StyleLibrary::write_sidecar(const StyleLibraryEntry& entry) const {
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

bool StyleLibrary::write_entry_asl(const StyleLibraryEntry& entry,
                                   std::span<const PatternResource> patterns) const {
  psd::AslStyle style;
  style.id = utf8_from_qstring(entry.id);
  style.name = utf8_from_qstring(entry.name);
  style.style = entry.style;
  style.blend_settings = entry.blend_settings;
  const auto embedded = referenced_patterns(entry.style, patterns);
  const auto bytes = psd::write_asl(std::span<const psd::AslStyle>(&style, 1), embedded);
  QSaveFile file(asl_path(entry.storage_id));
  if (!file.open(QIODevice::WriteOnly) ||
      file.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<qint64>(bytes.size())) != static_cast<qint64>(bytes.size())) {
    return false;
  }
  return file.commit();
}

QString StyleLibrary::add_style_internal(const QString& name, const LayerStyle& style,
                                         const std::optional<psd::AslBlendSettings>& blend_settings,
                                         std::span<const PatternResource> patterns,
                                         const QString& folder, const QString& requested_style_id,
                                         const QString& source_id) {
  auto style_id = requested_style_id;
  if (style_id.isEmpty()) {
    do {
      style_id = fresh_style_id();
    } while (find_entry_by_style_id(style_id) != nullptr);
  } else if (find_entry_by_style_id(style_id) != nullptr) {
    return {};
  }
  if (!QDir().mkpath(storage_dir_)) {
    return {};
  }

  QString storage_id;
  do {
    storage_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
  } while (QFileInfo::exists(asl_path(storage_id)) || QFileInfo::exists(json_path(storage_id)));

  StyleLibraryEntry entry;
  entry.storage_id = storage_id;
  entry.id = style_id;
  entry.source_id = source_id;
  entry.name = name.trimmed().isEmpty() ? tr("Untitled Style") : name.trimmed();
  entry.folder = folder.trimmed();
  entry.style = style;
  entry.blend_settings = blend_settings;
  if (!write_sidecar(entry)) {
    return {};
  }
  if (!write_entry_asl(entry, patterns)) {
    // reload() is .asl-driven, so a sidecar left behind by a failed cleanup can
    // never resurrect a style that was not fully saved.
    (void)QFile::remove(json_path(storage_id));
    return {};
  }
  refresh_thumbnail(entry, referenced_patterns(entry.style, patterns), true);
  entries_.push_back(std::move(entry));
  return storage_id;
}

QString StyleLibrary::add_style(const QString& name, const LayerStyle& style,
                                const std::optional<psd::AslBlendSettings>& blend_settings,
                                std::span<const PatternResource> patterns, const QString& folder,
                                const QString& preferred_style_id) {
  const auto storage_id =
      add_style_internal(name, style, blend_settings, patterns, folder, preferred_style_id);
  if (!storage_id.isEmpty()) {
    sort_entries();
    emit changed();
  }
  return storage_id;
}

QString StyleLibrary::import_asl(const QString& path, QString& error, QStringList& warnings) {
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
  if (file_size > kMaxAslFileBytes) {
    error = tr("\"%1\" is too large to import safely.").arg(QFileInfo(path).fileName());
    return {};
  }
  const auto bytes = file.readAll();
  if (file.error() != QFileDevice::NoError || bytes.size() != file_size) {
    error = tr("Could not read \"%1\".").arg(QFileInfo(path).fileName());
    return {};
  }
  std::string parse_error;
  const auto parsed = psd::read_asl(
      std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(bytes.constData()),
                                    static_cast<std::size_t>(bytes.size())),
      parse_error);
  if (!parsed.has_value()) {
    error = tr("Could not import styles from \"%1\". The file is not a supported Photoshop ASL "
               "file or is damaged.")
                .arg(QFileInfo(path).fileName());
    return {};
  }
  for (const auto& warning : parsed->warnings) {
    warnings.append(qstring_from_utf8(warning));
  }

  auto folder = QFileInfo(path).completeBaseName().trimmed();
  if (folder.isEmpty()) {
    folder = tr("Imported Styles");
  }
  QString first_storage_id;
  int added = 0;
  int style_index = 0;
  for (const auto& imported : parsed->styles) {
    ++style_index;
    auto name = qstring_from_utf8(imported.name).trimmed();
    if (name.isEmpty()) {
      name = tr("Style %1").arg(style_index);
    }

    const auto source_style_id = qstring_from_utf8(imported.id);
    auto style_id = source_style_id;
    if (style_id.isEmpty()) {
      style_id = fresh_style_id();
    }

    const auto matches_entry = [&](const StyleLibraryEntry& candidate) {
      return styles_equal(candidate.style, imported.style) &&
             candidate.blend_settings == imported.blend_settings;
    };
    const StyleLibraryEntry* matching_entry = nullptr;
    const auto* exact_id_entry = find_entry_by_style_id(style_id);
    if (exact_id_entry != nullptr && matches_entry(*exact_id_entry)) {
      matching_entry = exact_id_entry;
    }
    if (matching_entry == nullptr && !source_style_id.isEmpty()) {
      for (const auto& candidate : entries_) {
        if (candidate.source_id == source_style_id && matches_entry(candidate)) {
          matching_entry = &candidate;
          break;
        }
      }
    }
    if (matching_entry != nullptr) {
      if (first_storage_id.isEmpty()) {
        first_storage_id = matching_entry->storage_id;
      }
      continue;  // same source identity and recipe: already installed
    }

    QString remapped_source_id;
    if (exact_id_entry != nullptr) {
      do {
        style_id = fresh_style_id();
      } while (find_entry_by_style_id(style_id) != nullptr);
      remapped_source_id = source_style_id;
      warnings.append(
          tr("Style \"%1\" used an id already assigned to a different style; it was imported with a new id.")
              .arg(name));
    }

    const auto storage_id = add_style_internal(name, imported.style, imported.blend_settings,
                                               parsed->patterns, folder, style_id,
                                               remapped_source_id);
    if (storage_id.isEmpty()) {
      warnings.append(tr("Could not save style \"%1\".").arg(name));
      continue;
    }
    ++added;
    if (first_storage_id.isEmpty()) {
      first_storage_id = storage_id;
    }
  }
  if (first_storage_id.isEmpty()) {
    error = tr("No styles could be imported from \"%1\".").arg(QFileInfo(path).fileName());
    return {};
  }
  if (added > 0) {
    sort_entries();
    emit changed();
  }
  return first_storage_id;
}

bool StyleLibrary::export_asl(const QStringList& storage_ids, const QString& path,
                              QString& error) const {
  error.clear();
  std::vector<psd::AslStyle> styles;
  std::vector<PatternResource> patterns;
  for (const auto& storage_id : storage_ids) {
    const auto* entry = find_entry(storage_id);
    if (entry == nullptr) {
      continue;
    }
    psd::AslStyle style;
    style.id = utf8_from_qstring(entry->id);
    style.name = utf8_from_qstring(entry->name);
    style.style = entry->style;
    style.blend_settings = entry->blend_settings;
    styles.push_back(std::move(style));
    for (const auto& pattern : referenced_patterns(entry->style, patterns_for_entry(storage_id))) {
      const auto exists = std::any_of(patterns.begin(), patterns.end(),
                                      [&pattern](const PatternResource& existing) {
                                        return existing.id == pattern.id;
                                      });
      if (!exists) {
        patterns.push_back(pattern);
      }
    }
  }
  if (styles.empty()) {
    error = tr("There are no styles to export.");
    return false;
  }
  const auto bytes = psd::write_asl(styles, patterns);
  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly) ||
      file.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<qint64>(bytes.size())) != static_cast<qint64>(bytes.size()) ||
      !file.commit()) {
    error = tr("Could not write \"%1\".").arg(QFileInfo(path).fileName());
    return false;
  }
  return true;
}

QString StyleLibrary::duplicate_style(const QString& storage_id) {
  const auto* entry = find_entry(storage_id);
  if (entry == nullptr) {
    return {};
  }
  const auto copy_name = tr("%1 Copy").arg(style_library_entry_display_name(*entry));
  const auto patterns = patterns_for_entry(storage_id);
  const auto duplicate_id =
      add_style_internal(copy_name, entry->style, entry->blend_settings, patterns, entry->folder, {});
  if (!duplicate_id.isEmpty()) {
    sort_entries();
    emit changed();
  }
  return duplicate_id;
}

bool StyleLibrary::rename_style(const QString& storage_id, const QString& name) {
  const auto trimmed = name.trimmed();
  const auto found = std::find_if(entries_.begin(), entries_.end(),
                                  [&storage_id](const StyleLibraryEntry& entry) {
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

bool StyleLibrary::set_style_folder(const QString& storage_id, const QString& folder) {
  const auto found = std::find_if(entries_.begin(), entries_.end(),
                                  [&storage_id](const StyleLibraryEntry& entry) {
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

bool StyleLibrary::remove_style_internal(const QString& storage_id) {
  const auto found = std::find_if(entries_.begin(), entries_.end(),
                                  [&storage_id](const StyleLibraryEntry& entry) {
                                    return entry.storage_id == storage_id;
                                  });
  if (found == entries_.end()) {
    return false;
  }
  const auto entry_path = asl_path(storage_id);
  if (QFileInfo::exists(entry_path) && !QFile::remove(entry_path) &&
      QFileInfo::exists(entry_path)) {
    return false;
  }
  (void)QFile::remove(json_path(storage_id));
  (void)QFile::remove(thumbnail_path(storage_id));
  invalidate_cached_patterns(storage_id);
  entries_.erase(found);
  return true;
}

bool StyleLibrary::remove_style(const QString& storage_id) {
  if (!remove_style_internal(storage_id)) {
    return false;
  }
  emit changed();
  return true;
}

int StyleLibrary::remove_styles(const QStringList& storage_ids) {
  int removed = 0;
  for (const auto& storage_id : storage_ids) {
    if (remove_style_internal(storage_id)) {
      ++removed;
    }
  }
  if (removed > 0) {
    emit changed();
  }
  return removed;
}

int StyleLibrary::restore_default_styles(int newer_than_version) {
  int restored = 0;
  for (const auto& preset : builtin_style_presets()) {
    if (preset.introduced_version <= newer_than_version) {
      continue;
    }
    const auto style_id = QString::fromLatin1(preset.id);
    if (find_entry_by_style_id(style_id) != nullptr) {
      continue;
    }
    const auto style = builtin_style_preset_style(preset.id);
    const auto patterns = referenced_patterns(style, {});
    if (!add_style_internal(QString::fromLatin1(preset.english_name), style, std::nullopt,
                            patterns, style_preset_folder_display_name(preset.english_folder),
                            style_id)
             .isEmpty()) {
      ++restored;
    }
  }
  if (restored > 0) {
    sort_entries();
    emit changed();
  }
  return restored;
}

bool StyleLibrary::has_all_default_styles_introduced_after(int newer_than_version) const {
  const auto presets = builtin_style_presets();
  return std::all_of(presets.begin(), presets.end(),
                     [this, newer_than_version](const StylePreset& preset) {
                       return preset.introduced_version <= newer_than_version ||
                              find_entry_by_style_id(QString::fromLatin1(preset.id)) != nullptr;
                     });
}

bool StyleLibrary::default_styles_match_factory() const {
  for (const auto& preset : builtin_style_presets()) {
    const auto* entry = find_entry_by_style_id(QString::fromLatin1(preset.id));
    if (entry == nullptr || !entry->source_id.isEmpty() ||
        entry->name != QString::fromLatin1(preset.english_name) ||
        entry->folder != style_preset_folder_display_name(preset.english_folder)) {
      return false;
    }
    const auto expected_style = builtin_style_preset_style(preset.id);
    if (!styles_equal(entry->style, expected_style) || entry->blend_settings.has_value()) {
      return false;
    }
    const auto embedded = patterns_for_entry(entry->storage_id);
    for (const auto& expected : referenced_patterns(expected_style, {})) {
      const auto found = std::find_if(embedded.begin(), embedded.end(),
                                      [&expected](const PatternResource& pattern) {
                                        return pattern.id == expected.id;
                                      });
      if (found == embedded.end() || !pattern_tiles_equal(found->tile, expected.tile)) {
        return false;
      }
    }
  }
  return true;
}

int StyleLibrary::reset_default_styles_to_factory() {
  int reset = 0;
  bool any_changed = false;
  for (const auto& preset : builtin_style_presets()) {
    const auto style_id = QString::fromLatin1(preset.id);
    const auto found = std::find_if(entries_.begin(), entries_.end(),
                                    [&style_id](const StyleLibraryEntry& entry) {
                                      return entry.id == style_id;
                                    });
    if (found == entries_.end()) {
      continue;  // restore_default_styles() owns missing entries
    }
    const auto expected_style = builtin_style_preset_style(preset.id);
    const auto expected_patterns = referenced_patterns(expected_style, {});
    const auto canonical_name = QString::fromLatin1(preset.english_name);
    const auto canonical_folder = style_preset_folder_display_name(preset.english_folder);

    auto recipe_changed =
        !styles_equal(found->style, expected_style) || found->blend_settings.has_value();
    if (!recipe_changed) {
      const auto embedded = patterns_for_entry(found->storage_id);
      for (const auto& expected : expected_patterns) {
        const auto pattern = std::find_if(embedded.begin(), embedded.end(),
                                          [&expected](const PatternResource& candidate) {
                                            return candidate.id == expected.id;
                                          });
        if (pattern == embedded.end() || !pattern_tiles_equal(pattern->tile, expected.tile)) {
          recipe_changed = true;
          break;
        }
      }
    }
    const auto metadata_changed = found->name != canonical_name ||
                                  found->folder != canonical_folder ||
                                  !found->source_id.isEmpty();
    if (!recipe_changed && !metadata_changed) {
      continue;
    }

    auto updated = *found;
    updated.name = canonical_name;
    updated.folder = canonical_folder;
    updated.source_id.clear();
    updated.style = expected_style;
    updated.blend_settings.reset();
    auto saved = true;
    if (recipe_changed && !write_entry_asl(updated, expected_patterns)) {
      saved = false;
    }
    if (saved && metadata_changed && !write_sidecar(updated)) {
      saved = false;
    }
    if (!saved) {
      continue;
    }
    invalidate_cached_patterns(found->storage_id);
    refresh_thumbnail(updated, expected_patterns, true);
    *found = std::move(updated);
    any_changed = true;
    ++reset;
  }
  if (any_changed) {
    sort_entries();
    emit changed();
  }
  return reset;
}

}  // namespace patchy::ui
