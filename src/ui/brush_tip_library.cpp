#include "ui/brush_tip_library.hpp"

#include "psd/abr_reader.hpp"
#include "ui/app_settings.hpp"
#include "ui/default_brush_tips.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QUuid>

#include <algorithm>
#include <span>
#include <utility>

namespace patchy::ui {

namespace {

constexpr int kMaxTipDimension = 4096;
constexpr std::size_t kTipCacheLimit = 16;

[[nodiscard]] double clamp_spacing(double spacing) {
  return std::clamp(spacing, 0.01, 10.0);
}

[[nodiscard]] QString default_storage_dir() {
  const auto settings = app_settings();
  return QFileInfo(settings.fileName()).absolutePath() + QStringLiteral("/brushes");
}

// Crops to the non-zero bounding box; returns false when the mask is entirely empty.
bool crop_brush_tip_to_content(patchy::BrushTip& tip) {
  std::int32_t min_x = tip.width;
  std::int32_t min_y = tip.height;
  std::int32_t max_x = -1;
  std::int32_t max_y = -1;
  for (std::int32_t y = 0; y < tip.height; ++y) {
    const auto* row = tip.mask.data() + static_cast<std::size_t>(y) * tip.width;
    for (std::int32_t x = 0; x < tip.width; ++x) {
      if (row[x] != 0U) {
        min_x = std::min(min_x, x);
        min_y = std::min(min_y, y);
        max_x = std::max(max_x, x);
        max_y = std::max(max_y, y);
      }
    }
  }
  if (max_x < min_x || max_y < min_y) {
    return false;
  }
  const auto width = max_x - min_x + 1;
  const auto height = max_y - min_y + 1;
  if (width == tip.width && height == tip.height) {
    return true;
  }
  std::vector<std::uint8_t> cropped(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
  for (std::int32_t y = 0; y < height; ++y) {
    const auto* src = tip.mask.data() + static_cast<std::size_t>(y + min_y) * tip.width + min_x;
    std::copy_n(src, width, cropped.data() + static_cast<std::size_t>(y) * width);
  }
  tip.width = width;
  tip.height = height;
  tip.mask = std::move(cropped);
  return true;
}

}  // namespace

const QString& builtin_round_brush_tip_id() {
  static const QString id = QStringLiteral("builtin.round");
  return id;
}

patchy::BrushTip brush_tip_from_coverage_image(const QImage& coverage_mask, double spacing) {
  patchy::BrushTip tip;
  tip.default_spacing = clamp_spacing(spacing);
  if (coverage_mask.isNull()) {
    return tip;
  }
  const auto gray = coverage_mask.convertToFormat(QImage::Format_Grayscale8);
  if (gray.isNull() || gray.width() <= 0 || gray.height() <= 0) {
    return tip;
  }
  tip.width = gray.width();
  tip.height = gray.height();
  tip.mask.resize(static_cast<std::size_t>(tip.width) * static_cast<std::size_t>(tip.height));
  for (int y = 0; y < gray.height(); ++y) {
    const auto* row = gray.constScanLine(y);
    std::copy_n(row, tip.width, tip.mask.data() + static_cast<std::size_t>(y) * tip.width);
  }
  return tip;
}

QImage coverage_image_from_brush_tip(const patchy::BrushTip& tip) {
  if (tip.empty()) {
    return {};
  }
  QImage image(tip.width, tip.height, QImage::Format_Grayscale8);
  for (int y = 0; y < tip.height; ++y) {
    auto* row = image.scanLine(y);
    std::copy_n(tip.mask.data() + static_cast<std::size_t>(y) * tip.width, tip.width, row);
  }
  return image;
}

QPixmap brush_tip_thumbnail(const patchy::BrushTip& tip, int extent) {
  QImage canvas(extent, extent, QImage::Format_ARGB32_Premultiplied);
  canvas.fill(Qt::transparent);
  QPainter painter(&canvas);
  painter.setRenderHint(QPainter::Antialiasing);
  // Light paper chip behind the ink so black tips stay visible on the dark UI (matches
  // round_tip_thumbnail in brush_tip_picker.cpp).
  painter.setPen(QColor(0, 0, 0, 70));
  painter.setBrush(QColor(0xE9, 0xE9, 0xE9));
  painter.drawRoundedRect(QRectF(0.5, 0.5, extent - 1.0, extent - 1.0), 3.5, 3.5);
  if (!tip.empty()) {
    // Ink-on-paper preview: coverage becomes black ink alpha, like Photoshop's brush picker.
    QImage ink(tip.width, tip.height, QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < tip.height; ++y) {
      auto* out = reinterpret_cast<QRgb*>(ink.scanLine(y));
      const auto* mask_row = tip.mask.data() + static_cast<std::size_t>(y) * tip.width;
      for (int x = 0; x < tip.width; ++x) {
        out[x] = qRgba(0, 0, 0, mask_row[x]);
      }
    }
    const auto inset = std::max(2, extent / 12);
    const auto fitted = ink.scaled(extent - 2 * inset, extent - 2 * inset, Qt::KeepAspectRatio,
                                   Qt::SmoothTransformation);
    painter.drawImage((extent - fitted.width()) / 2, (extent - fitted.height()) / 2, fitted);
  }
  painter.end();
  return QPixmap::fromImage(canvas);
}

struct BrushTipLibrary::StoredTip {
  QString name;
  double spacing{0.25};
};

BrushTipLibrary::BrushTipLibrary(QString storage_dir, QObject* parent)
    : QObject(parent), storage_dir_(storage_dir.isEmpty() ? default_storage_dir() : std::move(storage_dir)) {
  reload();
}

const QString& BrushTipLibrary::storage_dir() const noexcept {
  return storage_dir_;
}

const std::vector<BrushTipEntry>& BrushTipLibrary::entries() const noexcept {
  return entries_;
}

const BrushTipEntry* BrushTipLibrary::find_entry(const QString& id) const {
  const auto found = std::find_if(entries_.begin(), entries_.end(),
                                  [&id](const BrushTipEntry& entry) { return entry.id == id; });
  return found == entries_.end() ? nullptr : &*found;
}

QString BrushTipLibrary::png_path(const QString& id) const {
  return storage_dir_ + QStringLiteral("/") + id + QStringLiteral(".png");
}

QString BrushTipLibrary::json_path(const QString& id) const {
  return storage_dir_ + QStringLiteral("/") + id + QStringLiteral(".json");
}

void BrushTipLibrary::reload() {
  entries_.clear();
  tip_cache_.clear();
  QDir dir(storage_dir_);
  if (!dir.exists()) {
    return;
  }
  const auto png_files = dir.entryInfoList({QStringLiteral("*.png")}, QDir::Files, QDir::Name);
  for (const auto& file_info : png_files) {
    const auto id = file_info.completeBaseName();
    QImage mask(file_info.absoluteFilePath());
    if (mask.isNull()) {
      continue;  // corrupt mask: skip this tip, keep the rest of the library
    }
    BrushTipEntry entry;
    entry.id = id;
    entry.name = id;
    entry.spacing = 0.25;
    QFile sidecar(json_path(id));
    if (sidecar.open(QIODevice::ReadOnly)) {
      const auto document = QJsonDocument::fromJson(sidecar.readAll());
      if (document.isObject()) {
        const auto object = document.object();
        const auto name = object.value(QStringLiteral("name")).toString();
        if (!name.isEmpty()) {
          entry.name = name;
        }
        entry.spacing = clamp_spacing(object.value(QStringLiteral("spacing")).toDouble(0.25));
        entry.folder = object.value(QStringLiteral("folder")).toString().trimmed();
      }
    }
    entry.size = mask.size();
    entry.thumbnail = brush_tip_thumbnail(brush_tip_from_coverage_image(mask, entry.spacing), 48);
    entries_.push_back(std::move(entry));
  }
  sort_entries();
}

void BrushTipLibrary::sort_entries() {
  // Ungrouped tips first, then folders alphabetically; by name inside each group.
  std::sort(entries_.begin(), entries_.end(), [](const BrushTipEntry& a, const BrushTipEntry& b) {
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
    return a.id < b.id;
  });
}

QStringList BrushTipLibrary::folders() const {
  QStringList folders;
  for (const auto& entry : entries_) {
    if (!entry.folder.isEmpty() && !folders.contains(entry.folder)) {
      folders.append(entry.folder);
    }
  }
  return folders;
}

std::shared_ptr<const patchy::BrushTip> BrushTipLibrary::tip(const QString& id) const {
  if (id.isEmpty() || id == builtin_round_brush_tip_id()) {
    return nullptr;
  }
  for (auto& cached : tip_cache_) {
    if (cached.first == id) {
      return cached.second;
    }
  }
  const auto* entry = find_entry(id);
  if (entry == nullptr) {
    return nullptr;
  }
  QImage mask(png_path(id));
  if (mask.isNull()) {
    return nullptr;
  }
  auto loaded = std::make_shared<patchy::BrushTip>(brush_tip_from_coverage_image(mask, entry->spacing));
  if (loaded->empty()) {
    return nullptr;
  }
  if (tip_cache_.size() >= kTipCacheLimit) {
    tip_cache_.erase(tip_cache_.begin());
  }
  tip_cache_.emplace_back(id, loaded);
  return loaded;
}

bool BrushTipLibrary::write_sidecar(const QString& id, const QString& name, double spacing,
                                    const QString& folder) const {
  QJsonObject object;
  object.insert(QStringLiteral("name"), name);
  object.insert(QStringLiteral("spacing"), clamp_spacing(spacing));
  if (!folder.trimmed().isEmpty()) {
    object.insert(QStringLiteral("folder"), folder.trimmed());
  }
  QFile file(json_path(id));
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    return false;
  }
  file.write(QJsonDocument(object).toJson(QJsonDocument::Indented));
  return true;
}

QString BrushTipLibrary::add_tip_internal(const QString& name, const QImage& coverage_mask, double spacing,
                                          const QString& folder) {
  auto tip = brush_tip_from_coverage_image(coverage_mask, spacing);
  if (tip.empty()) {
    return {};
  }
  if (!crop_brush_tip_to_content(tip)) {
    return {};  // entirely empty mask
  }
  if (tip.width > kMaxTipDimension || tip.height > kMaxTipDimension) {
    return {};
  }
  if (!QDir().mkpath(storage_dir_)) {
    return {};
  }
  const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
  if (!coverage_image_from_brush_tip(tip).save(png_path(id), "PNG")) {
    return {};
  }
  if (!write_sidecar(id, name, spacing, folder)) {
    QFile::remove(png_path(id));
    return {};
  }
  BrushTipEntry entry;
  entry.id = id;
  entry.name = name;
  entry.folder = folder.trimmed();
  entry.spacing = clamp_spacing(spacing);
  entry.size = QSize(tip.width, tip.height);
  entry.thumbnail = brush_tip_thumbnail(tip, 48);
  entries_.push_back(std::move(entry));
  return id;
}

QString BrushTipLibrary::add_tip(const QString& name, const QImage& coverage_mask, double spacing,
                                 const QString& folder) {
  const auto id = add_tip_internal(name, coverage_mask, spacing, folder);
  if (!id.isEmpty()) {
    sort_entries();
    emit changed();
  }
  return id;
}

QString BrushTipLibrary::import_abr(const QString& path, QString& error, QStringList& warnings) {
  error.clear();
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    error = tr("Could not open \"%1\".").arg(QFileInfo(path).fileName());
    return {};
  }
  const auto bytes = file.readAll();
  std::string parse_error;
  const auto result = psd::read_abr(
      std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(bytes.constData()),
                                    static_cast<std::size_t>(bytes.size())),
      parse_error);
  if (!result.has_value()) {
    error = QString::fromStdString(parse_error);
    return {};
  }
  for (const auto& warning : result->warnings) {
    warnings.append(QString::fromStdString(warning));
  }

  // Imported sets land in their own folder so large ABRs stay organized.
  const auto folder = QFileInfo(path).completeBaseName().trimmed();
  QString first_id;
  int brush_index = 0;
  for (const auto& brush : result->brushes) {
    ++brush_index;
    auto name = QString::fromStdString(brush.name).trimmed();
    if (name.isEmpty()) {
      name = tr("Brush %1").arg(brush_index);
    }
    patchy::BrushTip tip;
    tip.width = brush.width;
    tip.height = brush.height;
    tip.mask = brush.mask;
    tip.default_spacing = clamp_spacing(brush.spacing);
    const auto id = add_tip_internal(name, coverage_image_from_brush_tip(tip), tip.default_spacing, folder);
    if (id.isEmpty()) {
      warnings.append(tr("Could not save brush \"%1\".").arg(name));
      continue;
    }
    if (first_id.isEmpty()) {
      first_id = id;
    }
  }
  if (first_id.isEmpty()) {
    if (error.isEmpty()) {
      error = tr("No brush tips could be imported from \"%1\".").arg(QFileInfo(path).fileName());
    }
    return {};
  }
  sort_entries();
  emit changed();
  return first_id;
}

int BrushTipLibrary::restore_default_tips() {
  const auto folder = default_brush_tips_folder_name();
  int restored = 0;
  for (const auto& spec : generate_default_brush_tips()) {
    const auto exists = std::any_of(entries_.begin(), entries_.end(), [&](const BrushTipEntry& entry) {
      return entry.folder == folder && entry.name == spec.name;
    });
    if (exists) {
      continue;
    }
    if (!add_tip_internal(spec.name, coverage_image_from_brush_tip(spec.tip), spec.spacing, folder)
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

bool BrushTipLibrary::rename_tip(const QString& id, const QString& name) {
  const auto trimmed = name.trimmed();
  const auto found = std::find_if(entries_.begin(), entries_.end(),
                                  [&id](const BrushTipEntry& entry) { return entry.id == id; });
  if (found == entries_.end() || trimmed.isEmpty()) {
    return false;
  }
  if (!write_sidecar(id, trimmed, found->spacing, found->folder)) {
    return false;
  }
  found->name = trimmed;
  sort_entries();
  emit changed();
  return true;
}

bool BrushTipLibrary::remove_tip_internal(const QString& id) {
  const auto found = std::find_if(entries_.begin(), entries_.end(),
                                  [&id](const BrushTipEntry& entry) { return entry.id == id; });
  if (found == entries_.end()) {
    return false;
  }
  QFile::remove(png_path(id));
  QFile::remove(json_path(id));
  entries_.erase(found);
  tip_cache_.erase(std::remove_if(tip_cache_.begin(), tip_cache_.end(),
                                  [&id](const auto& cached) { return cached.first == id; }),
                   tip_cache_.end());
  return true;
}

bool BrushTipLibrary::remove_tip(const QString& id) {
  if (!remove_tip_internal(id)) {
    return false;
  }
  emit changed();
  return true;
}

int BrushTipLibrary::remove_tips(const QStringList& ids) {
  int removed = 0;
  for (const auto& id : ids) {
    if (remove_tip_internal(id)) {
      ++removed;
    }
  }
  if (removed > 0) {
    emit changed();
  }
  return removed;
}

bool BrushTipLibrary::set_tip_spacing(const QString& id, double spacing) {
  const auto found = std::find_if(entries_.begin(), entries_.end(),
                                  [&id](const BrushTipEntry& entry) { return entry.id == id; });
  if (found == entries_.end()) {
    return false;
  }
  const auto clamped = clamp_spacing(spacing);
  if (!write_sidecar(id, found->name, clamped, found->folder)) {
    return false;
  }
  found->spacing = clamped;
  tip_cache_.erase(std::remove_if(tip_cache_.begin(), tip_cache_.end(),
                                  [&id](const auto& cached) { return cached.first == id; }),
                   tip_cache_.end());
  emit changed();
  return true;
}

bool BrushTipLibrary::set_tip_folder(const QString& id, const QString& folder) {
  const auto found = std::find_if(entries_.begin(), entries_.end(),
                                  [&id](const BrushTipEntry& entry) { return entry.id == id; });
  if (found == entries_.end()) {
    return false;
  }
  const auto trimmed = folder.trimmed();
  if (found->folder == trimmed) {
    return true;
  }
  if (!write_sidecar(id, found->name, found->spacing, trimmed)) {
    return false;
  }
  found->folder = trimmed;
  sort_entries();
  emit changed();
  return true;
}

}  // namespace patchy::ui
