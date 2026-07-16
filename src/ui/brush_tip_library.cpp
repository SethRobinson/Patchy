#include "ui/brush_tip_library.hpp"

#include "psd/abr_reader.hpp"
#include "ui/default_brush_tips.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QUuid>

#include <algorithm>
#include <limits>
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
    case patchy::BrushDynamicControl::StylusWheel: return QStringLiteral("stylusWheel");
    case patchy::BrushDynamicControl::GlobalDefault: return QStringLiteral("global");
    case patchy::BrushDynamicControl::Off: break;
  }
  return QStringLiteral("off");
}

// Unknown or missing tokens read as the slot's default (forward compatible): Off for the angle,
// GlobalDefault or Off for the other dynamics.
[[nodiscard]] patchy::BrushDynamicControl control_from_token(const QString& token,
                                                             patchy::BrushDynamicControl fallback) {
  if (token == QStringLiteral("off")) return patchy::BrushDynamicControl::Off;
  if (token == QStringLiteral("fade")) return patchy::BrushDynamicControl::Fade;
  if (token == QStringLiteral("penPressure")) return patchy::BrushDynamicControl::PenPressure;
  if (token == QStringLiteral("penTilt")) return patchy::BrushDynamicControl::PenTilt;
  if (token == QStringLiteral("penRotation")) return patchy::BrushDynamicControl::PenRotation;
  if (token == QStringLiteral("initialDirection")) return patchy::BrushDynamicControl::InitialDirection;
  if (token == QStringLiteral("direction")) return patchy::BrushDynamicControl::Direction;
  if (token == QStringLiteral("stylusWheel")) return patchy::BrushDynamicControl::StylusWheel;
  if (token == QStringLiteral("global")) return patchy::BrushDynamicControl::GlobalDefault;
  return fallback;
}

// Direction/InitialDirection are angle-only and GlobalDefault only exists for size/roundness/
// opacity; anything invalid for a non-angle slot degrades to that slot's default.
[[nodiscard]] patchy::BrushDynamicControl sanitize_non_angle_control(
    patchy::BrushDynamicControl control, patchy::BrushDynamicControl fallback) {
  switch (control) {
    case patchy::BrushDynamicControl::Direction:
    case patchy::BrushDynamicControl::InitialDirection:
      return fallback;
    case patchy::BrushDynamicControl::GlobalDefault:
      return fallback == patchy::BrushDynamicControl::GlobalDefault ? control : fallback;
    default:
      return control;
  }
}

[[nodiscard]] double clamp_fraction(double value) {
  return std::clamp(value, 0.0, 1.0);
}

[[nodiscard]] QString texture_style_token(patchy::BrushTextureStyle style) {
  switch (style) {
    case patchy::BrushTextureStyle::Canvas: return QStringLiteral("canvas");
    case patchy::BrushTextureStyle::Speckle: return QStringLiteral("speckle");
    case patchy::BrushTextureStyle::FineGrain: break;
  }
  return QStringLiteral("fineGrain");
}

[[nodiscard]] patchy::BrushTextureStyle texture_style_from_token(const QString& token) {
  if (token == QStringLiteral("canvas")) return patchy::BrushTextureStyle::Canvas;
  if (token == QStringLiteral("speckle")) return patchy::BrushTextureStyle::Speckle;
  return patchy::BrushTextureStyle::FineGrain;
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

QString abr_import_summary(int imported, const QStringList& warnings) {
  auto text = QObject::tr("Imported %n brush tip(s).", nullptr, imported);
  int skipped = 0;
  int static_texture_depth = 0;
  for (const auto& warning : warnings) {
    if (warning.startsWith(QLatin1String("Skipped")) || warning.startsWith(QLatin1String("Ignored"))) {
      ++skipped;
    } else if (warning.contains(QLatin1String("input-driven texture depth"))) {
      ++static_texture_depth;
    }
  }
  if (skipped > 0) {
    text += QLatin1Char('\n') +
            QObject::tr("%n brush(es) could not be imported (no bitmap tip, or unreadable).", nullptr, skipped);
  }
  if (static_texture_depth > 0) {
    text += QLatin1Char('\n') +
            QObject::tr("%n brush(es) use input-driven texture depth; Patchy imported a static depth instead.",
                        nullptr, static_texture_depth);
  }
  return text;
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
    : BrushTipLibraryBase(std::move(storage_dir), parent) {
  reload();
}

QString BrushTipLibrary::png_path(const QString& id) const {
  return storage_path(id, ".png");
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
        if (object.contains(QStringLiteral("toolFlow"))) {
          entry.tool_flow_percent =
              std::clamp(object.value(QStringLiteral("toolFlow")).toInt(100), 1, 100);
        }
        if (object.contains(QStringLiteral("toolAirbrush"))) {
          entry.tool_airbrush = object.value(QStringLiteral("toolAirbrush")).toBool(false);
        }
      }
    }
    entry.size = mask.size();
    entry.thumbnail = brush_tip_thumbnail(brush_tip_from_coverage_image(mask, entry.spacing), 48);
    entries_.push_back(std::move(entry));
  }
  sort_entries();
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
  if (entry.tool_flow_percent.has_value()) {
    object.insert(QStringLiteral("toolFlow"), std::clamp(*entry.tool_flow_percent, 1, 100));
  }
  if (entry.tool_airbrush.has_value()) {
    object.insert(QStringLiteral("toolAirbrush"), *entry.tool_airbrush);
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
                                          double base_angle_degrees, double base_roundness,
                                          std::optional<int> tool_flow_percent,
                                          std::optional<bool> tool_airbrush) {
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
  entry.tool_flow_percent = tool_flow_percent;
  entry.tool_airbrush = tool_airbrush;
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
                                     brush.dynamics, brush.base_angle_degrees, brush.base_roundness,
                                     brush.tool_flow_percent, brush.tool_airbrush);
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

int BrushTipLibrary::restore_default_tips(int newer_than_version) {
  const auto folder = default_brush_tips_folder_name();
  int restored = 0;
  for (const auto& spec : generate_default_brush_tips()) {
    if (spec.since_version <= newer_than_version) {
      continue;  // shipped before the user's stored version; if missing, it was deleted on purpose
    }
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

namespace {

// Equality over the persisted dynamics fields only (seed/pen inputs are per-stroke scratch).
[[nodiscard]] bool persisted_dynamics_equal(const patchy::BrushDynamics& a,
                                            const patchy::BrushDynamics& b) {
  return a.size_jitter == b.size_jitter && a.minimum_diameter == b.minimum_diameter &&
         a.size_control == b.size_control && a.size_fade_steps == b.size_fade_steps &&
         a.angle_jitter == b.angle_jitter && a.angle_control == b.angle_control &&
         a.angle_fade_steps == b.angle_fade_steps && a.roundness_jitter == b.roundness_jitter &&
         a.minimum_roundness == b.minimum_roundness && a.roundness_control == b.roundness_control &&
         a.roundness_fade_steps == b.roundness_fade_steps && a.flip_x_jitter == b.flip_x_jitter &&
         a.flip_y_jitter == b.flip_y_jitter && a.scatter == b.scatter &&
         a.scatter_both_axes == b.scatter_both_axes && a.scatter_control == b.scatter_control &&
         a.scatter_fade_steps == b.scatter_fade_steps && a.count == b.count &&
         a.count_jitter == b.count_jitter && a.count_control == b.count_control &&
         a.count_fade_steps == b.count_fade_steps && a.opacity_jitter == b.opacity_jitter &&
         a.minimum_opacity == b.minimum_opacity && a.opacity_control == b.opacity_control &&
         a.opacity_fade_steps == b.opacity_fade_steps && a.flow_jitter == b.flow_jitter &&
         a.minimum_flow == b.minimum_flow && a.flow_control == b.flow_control &&
         a.flow_fade_steps == b.flow_fade_steps &&
         a.texture_enabled == b.texture_enabled && a.texture_style == b.texture_style &&
         a.texture_scale == b.texture_scale && a.texture_depth == b.texture_depth &&
         a.texture_invert == b.texture_invert && a.texture_seed == b.texture_seed &&
         a.dual_brush_enabled == b.dual_brush_enabled &&
         a.dual_brush_size == b.dual_brush_size &&
         a.dual_brush_hardness == b.dual_brush_hardness &&
         a.dual_brush_spacing == b.dual_brush_spacing &&
         a.color_dynamics_enabled == b.color_dynamics_enabled &&
         a.foreground_background_jitter == b.foreground_background_jitter &&
         a.color_control == b.color_control && a.color_fade_steps == b.color_fade_steps &&
         a.hue_jitter == b.hue_jitter && a.saturation_jitter == b.saturation_jitter &&
         a.brightness_jitter == b.brightness_jitter && a.purity == b.purity &&
         a.color_per_tip == b.color_per_tip && a.wet_edges == b.wet_edges;
}

}  // namespace

int BrushTipLibrary::reset_default_tips_to_factory() {
  const auto folder = default_brush_tips_folder_name();
  int reset = 0;
  for (const auto& spec : generate_default_brush_tips()) {
    const auto found = std::find_if(entries_.begin(), entries_.end(), [&](const BrushTipEntry& entry) {
      return entry.folder == folder && entry.name == spec.name;
    });
    if (found == entries_.end()) {
      continue;  // missing tips are restore_default_tips()'s job
    }
    // The stored mask was cropped to content at seed time, so crop the factory tip the same
    // way before comparing; a differing mask means the tip pixels predate a generator fix.
    auto factory_tip = spec.tip;
    auto mask_differs = false;
    if (crop_brush_tip_to_content(factory_tip)) {
      const auto current = tip(found->id);
      mask_differs = current == nullptr || current->width != factory_tip.width ||
                     current->height != factory_tip.height || current->mask != factory_tip.mask;
    }
    const auto settings_match = found->spacing == spec.spacing &&
                                found->base_angle_degrees == 0.0 && found->base_roundness == 100.0 &&
                                persisted_dynamics_equal(found->dynamics, spec.dynamics) &&
                                !found->tool_flow_percent.has_value() &&
                                !found->tool_airbrush.has_value();
    if (settings_match && !mask_differs) {
      continue;
    }
    auto updated = *found;
    updated.spacing = spec.spacing;
    updated.base_angle_degrees = 0.0;
    updated.base_roundness = 100.0;
    updated.dynamics = spec.dynamics;
    updated.tool_flow_percent.reset();
    updated.tool_airbrush.reset();
    if (mask_differs) {
      // Rewrite the stamp pixels in place (same id/file) and refresh everything derived.
      if (!coverage_image_from_brush_tip(factory_tip).save(png_path(found->id), "PNG")) {
        continue;
      }
      tip_cache_.erase(std::remove_if(tip_cache_.begin(), tip_cache_.end(),
                                      [&](const auto& cached) { return cached.first == found->id; }),
                       tip_cache_.end());
      updated.size = QSize(factory_tip.width, factory_tip.height);
      updated.thumbnail = brush_tip_thumbnail(factory_tip, 48);
    }
    if (write_sidecar(updated)) {
      *found = updated;
      ++reset;
    }
  }
  if (reset > 0) {
    emit changed();
  }
  return reset;
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
  // A rename to the unchanged name still rewrites the sidecar (historical
  // brush behavior, unlike styles/patterns).
  return rename_entry(id, name, /*skip_unchanged=*/false,
                      [this](const BrushTipEntry& entry) { return write_sidecar(entry); });
}

bool BrushTipLibrary::remove_entry_files(const QString& id) {
  QFile::remove(png_path(id));
  QFile::remove(json_path(id));
  tip_cache_.erase(std::remove_if(tip_cache_.begin(), tip_cache_.end(),
                                  [&id](const auto& cached) { return cached.first == id; }),
                   tip_cache_.end());
  return true;
}

bool BrushTipLibrary::remove_tip(const QString& id) {
  return remove_entry(id,
                      [this](const QString& tip_id) { return remove_entry_files(tip_id); });
}

int BrushTipLibrary::remove_tips(const QStringList& ids) {
  return remove_entries(ids,
                        [this](const QString& tip_id) { return remove_entry_files(tip_id); });
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
  return set_entry_folder(id, folder, /*skip_unchanged=*/true,
                          [this](const BrushTipEntry& entry) { return write_sidecar(entry); });
}

QJsonObject brush_dynamics_to_json(const patchy::BrushDynamics& dynamics) {
  QJsonObject object;
  object.insert(QStringLiteral("sizeJitter"), dynamics.size_jitter);
  object.insert(QStringLiteral("minimumDiameter"), dynamics.minimum_diameter);
  object.insert(QStringLiteral("sizeControl"), control_token(dynamics.size_control));
  object.insert(QStringLiteral("sizeFadeSteps"), dynamics.size_fade_steps);
  object.insert(QStringLiteral("angleJitter"), dynamics.angle_jitter);
  object.insert(QStringLiteral("angleControl"), control_token(dynamics.angle_control));
  object.insert(QStringLiteral("angleFadeSteps"), dynamics.angle_fade_steps);
  object.insert(QStringLiteral("roundnessJitter"), dynamics.roundness_jitter);
  object.insert(QStringLiteral("minimumRoundness"), dynamics.minimum_roundness);
  object.insert(QStringLiteral("roundnessControl"), control_token(dynamics.roundness_control));
  object.insert(QStringLiteral("roundnessFadeSteps"), dynamics.roundness_fade_steps);
  object.insert(QStringLiteral("flipXJitter"), dynamics.flip_x_jitter);
  object.insert(QStringLiteral("flipYJitter"), dynamics.flip_y_jitter);
  object.insert(QStringLiteral("scatter"), dynamics.scatter);
  object.insert(QStringLiteral("scatterBothAxes"), dynamics.scatter_both_axes);
  object.insert(QStringLiteral("scatterControl"), control_token(dynamics.scatter_control));
  object.insert(QStringLiteral("scatterFadeSteps"), dynamics.scatter_fade_steps);
  object.insert(QStringLiteral("count"), dynamics.count);
  object.insert(QStringLiteral("countJitter"), dynamics.count_jitter);
  object.insert(QStringLiteral("countControl"), control_token(dynamics.count_control));
  object.insert(QStringLiteral("countFadeSteps"), dynamics.count_fade_steps);
  object.insert(QStringLiteral("opacityJitter"), dynamics.opacity_jitter);
  object.insert(QStringLiteral("minimumOpacity"), dynamics.minimum_opacity);
  object.insert(QStringLiteral("opacityControl"), control_token(dynamics.opacity_control));
  object.insert(QStringLiteral("opacityFadeSteps"), dynamics.opacity_fade_steps);
  object.insert(QStringLiteral("flowJitter"), dynamics.flow_jitter);
  object.insert(QStringLiteral("minimumFlow"), dynamics.minimum_flow);
  object.insert(QStringLiteral("flowControl"), control_token(dynamics.flow_control));
  object.insert(QStringLiteral("flowFadeSteps"), dynamics.flow_fade_steps);
  object.insert(QStringLiteral("textureEnabled"), dynamics.texture_enabled);
  object.insert(QStringLiteral("textureStyle"), texture_style_token(dynamics.texture_style));
  object.insert(QStringLiteral("textureScale"), dynamics.texture_scale);
  object.insert(QStringLiteral("textureDepth"), dynamics.texture_depth);
  object.insert(QStringLiteral("textureInvert"), dynamics.texture_invert);
  object.insert(QStringLiteral("textureSeed"), static_cast<double>(dynamics.texture_seed));
  object.insert(QStringLiteral("dualBrushEnabled"), dynamics.dual_brush_enabled);
  object.insert(QStringLiteral("dualBrushSize"), dynamics.dual_brush_size);
  object.insert(QStringLiteral("dualBrushHardness"), dynamics.dual_brush_hardness);
  object.insert(QStringLiteral("dualBrushSpacing"), dynamics.dual_brush_spacing);
  object.insert(QStringLiteral("colorDynamicsEnabled"), dynamics.color_dynamics_enabled);
  object.insert(QStringLiteral("foregroundBackgroundJitter"),
                dynamics.foreground_background_jitter);
  object.insert(QStringLiteral("colorControl"), control_token(dynamics.color_control));
  object.insert(QStringLiteral("colorFadeSteps"), dynamics.color_fade_steps);
  object.insert(QStringLiteral("hueJitter"), dynamics.hue_jitter);
  object.insert(QStringLiteral("saturationJitter"), dynamics.saturation_jitter);
  object.insert(QStringLiteral("brightnessJitter"), dynamics.brightness_jitter);
  object.insert(QStringLiteral("purity"), dynamics.purity);
  object.insert(QStringLiteral("colorPerTip"), dynamics.color_per_tip);
  object.insert(QStringLiteral("wetEdges"), dynamics.wet_edges);
  return object;
}

patchy::BrushDynamics brush_dynamics_from_json(const QJsonObject& object) {
  patchy::BrushDynamics dynamics;
  if (object.isEmpty()) {
    return dynamics;
  }
  const auto read_control = [&object](const char* key, patchy::BrushDynamicControl fallback) {
    return control_from_token(object.value(QLatin1String(key)).toString(), fallback);
  };
  const auto read_fade_steps = [&object](const char* key) {
    return std::clamp(object.value(QLatin1String(key)).toInt(25), 1, 9999);
  };
  dynamics.size_jitter = clamp_fraction(object.value(QStringLiteral("sizeJitter")).toDouble(0.0));
  dynamics.minimum_diameter = clamp_fraction(object.value(QStringLiteral("minimumDiameter")).toDouble(0.0));
  dynamics.size_control = sanitize_non_angle_control(
      read_control("sizeControl", patchy::BrushDynamicControl::GlobalDefault),
      patchy::BrushDynamicControl::GlobalDefault);
  dynamics.size_fade_steps = read_fade_steps("sizeFadeSteps");
  dynamics.angle_jitter = clamp_fraction(object.value(QStringLiteral("angleJitter")).toDouble(0.0));
  dynamics.angle_control = read_control("angleControl", patchy::BrushDynamicControl::Off);
  if (dynamics.angle_control == patchy::BrushDynamicControl::GlobalDefault) {
    dynamics.angle_control = patchy::BrushDynamicControl::Off;  // no global angle preference
  }
  dynamics.angle_fade_steps = read_fade_steps("angleFadeSteps");
  dynamics.roundness_jitter = clamp_fraction(object.value(QStringLiteral("roundnessJitter")).toDouble(0.0));
  dynamics.minimum_roundness =
      clamp_fraction(object.value(QStringLiteral("minimumRoundness")).toDouble(0.25));
  dynamics.roundness_control = sanitize_non_angle_control(
      read_control("roundnessControl", patchy::BrushDynamicControl::GlobalDefault),
      patchy::BrushDynamicControl::GlobalDefault);
  dynamics.roundness_fade_steps = read_fade_steps("roundnessFadeSteps");
  dynamics.flip_x_jitter = object.value(QStringLiteral("flipXJitter")).toBool(false);
  dynamics.flip_y_jitter = object.value(QStringLiteral("flipYJitter")).toBool(false);
  dynamics.scatter = std::clamp(object.value(QStringLiteral("scatter")).toDouble(0.0), 0.0, 10.0);
  dynamics.scatter_both_axes = object.value(QStringLiteral("scatterBothAxes")).toBool(false);
  dynamics.scatter_control =
      sanitize_non_angle_control(read_control("scatterControl", patchy::BrushDynamicControl::Off),
                                 patchy::BrushDynamicControl::Off);
  dynamics.scatter_fade_steps = read_fade_steps("scatterFadeSteps");
  dynamics.count = std::clamp(object.value(QStringLiteral("count")).toInt(1), 1, 16);
  dynamics.count_jitter = clamp_fraction(object.value(QStringLiteral("countJitter")).toDouble(0.0));
  dynamics.count_control =
      sanitize_non_angle_control(read_control("countControl", patchy::BrushDynamicControl::Off),
                                 patchy::BrushDynamicControl::Off);
  dynamics.count_fade_steps = read_fade_steps("countFadeSteps");
  dynamics.opacity_jitter = clamp_fraction(object.value(QStringLiteral("opacityJitter")).toDouble(0.0));
  dynamics.minimum_opacity = clamp_fraction(object.value(QStringLiteral("minimumOpacity")).toDouble(0.0));
  dynamics.opacity_control = sanitize_non_angle_control(
      read_control("opacityControl", patchy::BrushDynamicControl::GlobalDefault),
      patchy::BrushDynamicControl::GlobalDefault);
  dynamics.opacity_fade_steps = read_fade_steps("opacityFadeSteps");
  dynamics.flow_jitter = clamp_fraction(object.value(QStringLiteral("flowJitter")).toDouble(0.0));
  dynamics.minimum_flow = clamp_fraction(object.value(QStringLiteral("minimumFlow")).toDouble(0.0));
  dynamics.flow_control = sanitize_non_angle_control(
      read_control("flowControl", patchy::BrushDynamicControl::Off),
      patchy::BrushDynamicControl::Off);
  dynamics.flow_fade_steps = read_fade_steps("flowFadeSteps");
  dynamics.texture_enabled = object.value(QStringLiteral("textureEnabled")).toBool(false);
  dynamics.texture_style =
      texture_style_from_token(object.value(QStringLiteral("textureStyle")).toString());
  dynamics.texture_scale =
      std::clamp(object.value(QStringLiteral("textureScale")).toDouble(1.0), 0.01, 10.0);
  dynamics.texture_depth =
      clamp_fraction(object.value(QStringLiteral("textureDepth")).toDouble(0.5));
  dynamics.texture_invert = object.value(QStringLiteral("textureInvert")).toBool(false);
  dynamics.texture_seed = static_cast<std::uint32_t>(std::clamp(
      object.value(QStringLiteral("textureSeed")).toDouble(0x5A17C9E3U), 0.0,
      static_cast<double>(std::numeric_limits<std::uint32_t>::max())));
  dynamics.dual_brush_enabled = object.value(QStringLiteral("dualBrushEnabled")).toBool(false);
  dynamics.dual_brush_size =
      std::clamp(object.value(QStringLiteral("dualBrushSize")).toDouble(0.5), 0.05, 4.0);
  dynamics.dual_brush_hardness =
      clamp_fraction(object.value(QStringLiteral("dualBrushHardness")).toDouble(1.0));
  dynamics.dual_brush_spacing =
      std::clamp(object.value(QStringLiteral("dualBrushSpacing")).toDouble(1.0), 0.1, 10.0);
  dynamics.color_dynamics_enabled =
      object.value(QStringLiteral("colorDynamicsEnabled")).toBool(false);
  dynamics.foreground_background_jitter =
      clamp_fraction(object.value(QStringLiteral("foregroundBackgroundJitter")).toDouble(0.0));
  dynamics.color_control = sanitize_non_angle_control(
      read_control("colorControl", patchy::BrushDynamicControl::Off),
      patchy::BrushDynamicControl::Off);
  dynamics.color_fade_steps = read_fade_steps("colorFadeSteps");
  dynamics.hue_jitter = clamp_fraction(object.value(QStringLiteral("hueJitter")).toDouble(0.0));
  dynamics.saturation_jitter =
      clamp_fraction(object.value(QStringLiteral("saturationJitter")).toDouble(0.0));
  dynamics.brightness_jitter =
      clamp_fraction(object.value(QStringLiteral("brightnessJitter")).toDouble(0.0));
  dynamics.purity = std::clamp(object.value(QStringLiteral("purity")).toDouble(0.0), -1.0, 1.0);
  dynamics.color_per_tip = object.value(QStringLiteral("colorPerTip")).toBool(true);
  dynamics.wet_edges = object.value(QStringLiteral("wetEdges")).toBool(false);
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
         dynamics.size_control == defaults.size_control &&
         dynamics.size_fade_steps == defaults.size_fade_steps &&
         dynamics.angle_jitter == defaults.angle_jitter &&
         dynamics.angle_control == defaults.angle_control &&
         dynamics.angle_fade_steps == defaults.angle_fade_steps &&
         dynamics.roundness_jitter == defaults.roundness_jitter &&
         dynamics.minimum_roundness == defaults.minimum_roundness &&
         dynamics.roundness_control == defaults.roundness_control &&
         dynamics.roundness_fade_steps == defaults.roundness_fade_steps &&
         dynamics.flip_x_jitter == defaults.flip_x_jitter &&
         dynamics.flip_y_jitter == defaults.flip_y_jitter && dynamics.scatter == defaults.scatter &&
         dynamics.scatter_both_axes == defaults.scatter_both_axes &&
         dynamics.scatter_control == defaults.scatter_control &&
         dynamics.scatter_fade_steps == defaults.scatter_fade_steps && dynamics.count == defaults.count &&
         dynamics.count_jitter == defaults.count_jitter &&
         dynamics.count_control == defaults.count_control &&
         dynamics.count_fade_steps == defaults.count_fade_steps &&
         dynamics.opacity_jitter == defaults.opacity_jitter &&
         dynamics.minimum_opacity == defaults.minimum_opacity &&
         dynamics.opacity_control == defaults.opacity_control &&
         dynamics.opacity_fade_steps == defaults.opacity_fade_steps &&
         dynamics.flow_jitter == defaults.flow_jitter &&
         dynamics.minimum_flow == defaults.minimum_flow &&
         dynamics.flow_control == defaults.flow_control &&
         dynamics.flow_fade_steps == defaults.flow_fade_steps &&
         dynamics.texture_enabled == defaults.texture_enabled &&
         dynamics.texture_style == defaults.texture_style &&
         dynamics.texture_scale == defaults.texture_scale &&
         dynamics.texture_depth == defaults.texture_depth &&
         dynamics.texture_invert == defaults.texture_invert &&
         dynamics.texture_seed == defaults.texture_seed &&
         dynamics.dual_brush_enabled == defaults.dual_brush_enabled &&
         dynamics.dual_brush_size == defaults.dual_brush_size &&
         dynamics.dual_brush_hardness == defaults.dual_brush_hardness &&
         dynamics.dual_brush_spacing == defaults.dual_brush_spacing &&
         dynamics.color_dynamics_enabled == defaults.color_dynamics_enabled &&
         dynamics.foreground_background_jitter == defaults.foreground_background_jitter &&
         dynamics.color_control == defaults.color_control &&
         dynamics.color_fade_steps == defaults.color_fade_steps &&
         dynamics.hue_jitter == defaults.hue_jitter &&
         dynamics.saturation_jitter == defaults.saturation_jitter &&
         dynamics.brightness_jitter == defaults.brightness_jitter &&
         dynamics.purity == defaults.purity && dynamics.color_per_tip == defaults.color_per_tip &&
         dynamics.wet_edges == defaults.wet_edges;
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
