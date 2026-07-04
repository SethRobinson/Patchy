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

[[nodiscard]] QString control_token(patchy::BrushDynamicControl control) {
  switch (control) {
    case patchy::BrushDynamicControl::Fade: return QStringLiteral("fade");
    case patchy::BrushDynamicControl::PenPressure: return QStringLiteral("penPressure");
    case patchy::BrushDynamicControl::PenTilt: return QStringLiteral("penTilt");
    case patchy::BrushDynamicControl::PenRotation: return QStringLiteral("penRotation");
    case patchy::BrushDynamicControl::InitialDirection: return QStringLiteral("initialDirection");
    case patchy::BrushDynamicControl::Direction: return QStringLiteral("direction");
    case patchy::BrushDynamicControl::Off: break;
  }
  return QStringLiteral("off");
}

[[nodiscard]] patchy::BrushDynamicControl control_from_token(const QString& token) {
  if (token == QStringLiteral("fade")) return patchy::BrushDynamicControl::Fade;
  if (token == QStringLiteral("penPressure")) return patchy::BrushDynamicControl::PenPressure;
  if (token == QStringLiteral("penTilt")) return patchy::BrushDynamicControl::PenTilt;
  if (token == QStringLiteral("penRotation")) return patchy::BrushDynamicControl::PenRotation;
  if (token == QStringLiteral("initialDirection")) return patchy::BrushDynamicControl::InitialDirection;
  if (token == QStringLiteral("direction")) return patchy::BrushDynamicControl::Direction;
  return patchy::BrushDynamicControl::Off;  // unknown tokens read as Off (forward compatible)
}

[[nodiscard]] double clamp_fraction(double value) {
  return std::clamp(value, 0.0, 1.0);
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
        entry.base_angle_degrees =
            std::clamp(object.value(QStringLiteral("baseAngle")).toDouble(0.0), -180.0, 360.0);
        entry.base_roundness =
            std::clamp(object.value(QStringLiteral("baseRoundness")).toDouble(100.0), 1.0, 100.0);
        entry.dynamics = brush_dynamics_from_json(object.value(QStringLiteral("dynamics")).toObject());
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

bool BrushTipLibrary::write_sidecar(const BrushTipEntry& entry) const {
  QJsonObject object;
  object.insert(QStringLiteral("name"), entry.name);
  object.insert(QStringLiteral("spacing"), clamp_spacing(entry.spacing));
  if (!entry.folder.trimmed().isEmpty()) {
    object.insert(QStringLiteral("folder"), entry.folder.trimmed());
  }
  // Tip shape and dynamics are written only when non-default, so plain tips keep the compact
  // legacy sidecar (and older builds editing such tips lose nothing).
  if (entry.base_angle_degrees != 0.0) {
    object.insert(QStringLiteral("baseAngle"), entry.base_angle_degrees);
  }
  if (entry.base_roundness != 100.0) {
    object.insert(QStringLiteral("baseRoundness"), entry.base_roundness);
  }
  if (!brush_dynamics_is_default(entry.dynamics)) {
    object.insert(QStringLiteral("dynamics"), brush_dynamics_to_json(entry.dynamics));
  }
  QFile file(json_path(entry.id));
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    return false;
  }
  file.write(QJsonDocument(object).toJson(QJsonDocument::Indented));
  return true;
}

QString BrushTipLibrary::add_tip_internal(const QString& name, const QImage& coverage_mask, double spacing,
                                          const QString& folder, const patchy::BrushDynamics& dynamics,
                                          double base_angle_degrees, double base_roundness) {
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
  BrushTipEntry entry;
  entry.id = id;
  entry.name = name;
  entry.folder = folder.trimmed();
  entry.spacing = clamp_spacing(spacing);
  entry.base_angle_degrees = std::clamp(base_angle_degrees, -180.0, 360.0);
  entry.base_roundness = std::clamp(base_roundness, 1.0, 100.0);
  entry.dynamics = dynamics;
  entry.size = QSize(tip.width, tip.height);
  entry.thumbnail = brush_tip_thumbnail(tip, 48);
  if (!write_sidecar(entry)) {
    QFile::remove(png_path(id));
    return {};
  }
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
    const auto id = add_tip_internal(name, coverage_image_from_brush_tip(tip), tip.default_spacing, folder,
                                     brush.dynamics, brush.base_angle_degrees, brush.base_roundness);
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
    if (!add_tip_internal(spec.name, coverage_image_from_brush_tip(spec.tip), spec.spacing, folder,
                          spec.dynamics)
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

int BrushTipLibrary::apply_default_tip_dynamics() {
  // One-shot migration for installs seeded before the built-in tips carried curated dynamics
  // (defaultTipsVersion < 2). Only tips whose dynamics/tip shape are still untouched defaults
  // are upgraded — a user's customizations (including a deliberate reset AFTER this migration)
  // always win, because the version gate runs this at most once per version bump.
  const auto folder = default_brush_tips_folder_name();
  int updated = 0;
  for (const auto& spec : generate_default_brush_tips()) {
    if (brush_dynamics_is_default(spec.dynamics)) {
      continue;
    }
    const auto found = std::find_if(entries_.begin(), entries_.end(), [&](const BrushTipEntry& entry) {
      return entry.folder == folder && entry.name == spec.name;
    });
    if (found == entries_.end()) {
      continue;
    }
    if (!brush_dynamics_is_default(found->dynamics) || found->base_angle_degrees != 0.0 ||
        found->base_roundness != 100.0) {
      continue;  // customized by the user
    }
    auto updated_entry = *found;
    updated_entry.dynamics = spec.dynamics;
    if (write_sidecar(updated_entry)) {
      found->dynamics = spec.dynamics;
      ++updated;
    }
  }
  if (updated > 0) {
    emit changed();
  }
  return updated;
}

bool BrushTipLibrary::rename_tip(const QString& id, const QString& name) {
  const auto trimmed = name.trimmed();
  const auto found = std::find_if(entries_.begin(), entries_.end(),
                                  [&id](const BrushTipEntry& entry) { return entry.id == id; });
  if (found == entries_.end() || trimmed.isEmpty()) {
    return false;
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
  auto updated = *found;
  updated.spacing = clamped;
  if (!write_sidecar(updated)) {
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

QJsonObject brush_dynamics_to_json(const patchy::BrushDynamics& dynamics) {
  QJsonObject object;
  object.insert(QStringLiteral("sizeJitter"), dynamics.size_jitter);
  object.insert(QStringLiteral("minimumDiameter"), dynamics.minimum_diameter);
  object.insert(QStringLiteral("angleJitter"), dynamics.angle_jitter);
  object.insert(QStringLiteral("angleControl"), control_token(dynamics.angle_control));
  object.insert(QStringLiteral("angleFadeSteps"), dynamics.angle_fade_steps);
  object.insert(QStringLiteral("roundnessJitter"), dynamics.roundness_jitter);
  object.insert(QStringLiteral("minimumRoundness"), dynamics.minimum_roundness);
  object.insert(QStringLiteral("flipXJitter"), dynamics.flip_x_jitter);
  object.insert(QStringLiteral("flipYJitter"), dynamics.flip_y_jitter);
  object.insert(QStringLiteral("scatter"), dynamics.scatter);
  object.insert(QStringLiteral("scatterBothAxes"), dynamics.scatter_both_axes);
  object.insert(QStringLiteral("count"), dynamics.count);
  object.insert(QStringLiteral("countJitter"), dynamics.count_jitter);
  object.insert(QStringLiteral("opacityJitter"), dynamics.opacity_jitter);
  return object;
}

patchy::BrushDynamics brush_dynamics_from_json(const QJsonObject& object) {
  patchy::BrushDynamics dynamics;
  if (object.isEmpty()) {
    return dynamics;
  }
  dynamics.size_jitter = clamp_fraction(object.value(QStringLiteral("sizeJitter")).toDouble(0.0));
  dynamics.minimum_diameter = clamp_fraction(object.value(QStringLiteral("minimumDiameter")).toDouble(0.0));
  dynamics.angle_jitter = clamp_fraction(object.value(QStringLiteral("angleJitter")).toDouble(0.0));
  dynamics.angle_control = control_from_token(object.value(QStringLiteral("angleControl")).toString());
  dynamics.angle_fade_steps =
      std::clamp(object.value(QStringLiteral("angleFadeSteps")).toInt(25), 1, 9999);
  dynamics.roundness_jitter = clamp_fraction(object.value(QStringLiteral("roundnessJitter")).toDouble(0.0));
  dynamics.minimum_roundness =
      clamp_fraction(object.value(QStringLiteral("minimumRoundness")).toDouble(0.25));
  dynamics.flip_x_jitter = object.value(QStringLiteral("flipXJitter")).toBool(false);
  dynamics.flip_y_jitter = object.value(QStringLiteral("flipYJitter")).toBool(false);
  dynamics.scatter = std::clamp(object.value(QStringLiteral("scatter")).toDouble(0.0), 0.0, 10.0);
  dynamics.scatter_both_axes = object.value(QStringLiteral("scatterBothAxes")).toBool(false);
  dynamics.count = std::clamp(object.value(QStringLiteral("count")).toInt(1), 1, 16);
  dynamics.count_jitter = clamp_fraction(object.value(QStringLiteral("countJitter")).toDouble(0.0));
  dynamics.opacity_jitter = clamp_fraction(object.value(QStringLiteral("opacityJitter")).toDouble(0.0));
  return dynamics;
}

bool brush_tip_entry_has_dynamics(const BrushTipEntry& entry) {
  return !brush_dynamics_is_default(entry.dynamics) || entry.base_angle_degrees != 0.0 ||
         entry.base_roundness != 100.0;
}

QPixmap brush_tip_thumbnail_with_badge(const BrushTipEntry& entry) {
  if (!brush_tip_entry_has_dynamics(entry) || entry.thumbnail.isNull()) {
    return entry.thumbnail;
  }
  auto decorated = entry.thumbnail;
  QPainter painter(&decorated);
  painter.setRenderHint(QPainter::Antialiasing);
  const auto radius = std::max(4.0, static_cast<double>(decorated.width()) / 8.0);
  const QPointF center(decorated.width() - radius - 2.0, decorated.height() - radius - 2.0);
  painter.setPen(QPen(Qt::white, 1.5));
  painter.setBrush(QColor(0x2F, 0x75, 0xBD));  // matches the toolbar's checked/accent blue
  painter.drawEllipse(center, radius, radius);
  return decorated;
}

bool brush_dynamics_is_default(const patchy::BrushDynamics& dynamics) {
  const patchy::BrushDynamics defaults;
  return dynamics.size_jitter == defaults.size_jitter &&
         dynamics.minimum_diameter == defaults.minimum_diameter &&
         dynamics.angle_jitter == defaults.angle_jitter &&
         dynamics.angle_control == defaults.angle_control &&
         dynamics.angle_fade_steps == defaults.angle_fade_steps &&
         dynamics.roundness_jitter == defaults.roundness_jitter &&
         dynamics.minimum_roundness == defaults.minimum_roundness &&
         dynamics.flip_x_jitter == defaults.flip_x_jitter &&
         dynamics.flip_y_jitter == defaults.flip_y_jitter && dynamics.scatter == defaults.scatter &&
         dynamics.scatter_both_axes == defaults.scatter_both_axes && dynamics.count == defaults.count &&
         dynamics.count_jitter == defaults.count_jitter &&
         dynamics.opacity_jitter == defaults.opacity_jitter;
  // seed / pen_* are per-stroke inputs, deliberately ignored.
}

bool BrushTipLibrary::set_tip_dynamics(const QString& id, const patchy::BrushDynamics& dynamics,
                                       double base_angle_degrees, double base_roundness) {
  const auto found = std::find_if(entries_.begin(), entries_.end(),
                                  [&id](const BrushTipEntry& entry) { return entry.id == id; });
  if (found == entries_.end()) {
    return false;
  }
  auto updated = *found;
  updated.dynamics = dynamics;
  updated.base_angle_degrees = std::clamp(base_angle_degrees, -180.0, 360.0);
  updated.base_roundness = std::clamp(base_roundness, 1.0, 100.0);
  if (!write_sidecar(updated)) {
    return false;
  }
  found->dynamics = updated.dynamics;
  found->base_angle_degrees = updated.base_angle_degrees;
  found->base_roundness = updated.base_roundness;
  emit changed();
  return true;
}

}  // namespace patchy::ui
