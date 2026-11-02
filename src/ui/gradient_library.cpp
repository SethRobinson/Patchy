#include "ui/gradient_library.hpp"
#include "ui/localization.hpp"
#include "ui/transparency_checkerboard.hpp"

#include "core/blend_math.hpp"
#include "core/gradient_presets.hpp"
#include "psd/grd_io.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QSaveFile>
#include <QUuid>

#include <algorithm>
#include <cmath>

namespace patchy::ui {
namespace {

LayerStyleGradient render_gradient(const GradientDefinition &definition) {
  LayerStyleGradient result;
  static_cast<GradientDefinition &>(result) = definition;
  return result;
}

QByteArray bytes_for(const QString &name, const QString &folder,
                     const GradientDefinition &definition) {
  const patchy::psd::GrdGradient item{
      name.toUtf8().toStdString(), folder.toUtf8().toStdString(), definition};
  const auto bytes = patchy::psd::write_grd(
      std::span<const patchy::psd::GrdGradient>(&item, 1));
  return QByteArray(reinterpret_cast<const char *>(bytes.data()),
                    static_cast<qsizetype>(bytes.size()));
}

bool definitions_equal(const GradientDefinition &lhs,
                       const GradientDefinition &rhs) {
  return bytes_for(QStringLiteral("Compare"), {}, lhs) ==
         bytes_for(QStringLiteral("Compare"), {}, rhs);
}

QString canonical_folder(const char *folder) {
  return QString::fromLatin1(folder);
}

} // namespace

GradientDefinition resolve_gradient_definition(GradientDefinition definition,
                                               RgbColor foreground,
                                               RgbColor background) {
  for (auto &stop : definition.color_stops) {
    if (stop.kind == GradientColorStop::Kind::Foreground) {
      stop.color = foreground;
    } else if (stop.kind == GradientColorStop::Kind::Background) {
      stop.color = background;
    }
    stop.kind = GradientColorStop::Kind::User;
  }
  return definition;
}

bool gradient_has_dynamic_stops(const GradientDefinition &definition) {
  return definition.form == GradientDefinitionForm::Solid &&
         std::any_of(definition.color_stops.begin(), definition.color_stops.end(),
                     [](const GradientColorStop &stop) {
                       return stop.kind != GradientColorStop::Kind::User;
                     });
}

QPixmap gradient_thumbnail(const GradientDefinition &definition, int width,
                           int height,
                           const std::optional<GradientPreviewColors> &colors) {
  width = std::max(16, width);
  height = std::max(8, height);
  // The transparency checkerboard under the ramp shows its alpha (GitHub #581);
  // like the canvas's, it is theme-exempt.
  QImage image(width, height, QImage::Format_RGB32);
  {
    QPainter painter(&image);
    const int square = std::max(4, height / 4);
    for (int y = 0; y < height; y += square)
      for (int x = 0; x < width; x += square)
        painter.fillRect(x, y, square, square,
                         ((x / square) + (y / square)) % 2 == 0
                             ? checkerboard_light_square()
                             : checkerboard_dark_square());
  }
  auto gradient = render_gradient(
      colors ? resolve_gradient_definition(definition, colors->foreground,
                                           colors->background)
             : definition);
  for (int x = 0; x < width; ++x) {
    const auto position =
        width <= 1 ? 0.0F
                   : static_cast<float>(x) / static_cast<float>(width - 1);
    const auto color = gradient_color(gradient, position);
    const auto alpha =
        std::clamp(gradient_stop_opacity(gradient, position), 0.0F, 1.0F);
    auto *bits = reinterpret_cast<QRgb *>(image.bits());
    const auto stride = image.bytesPerLine() / static_cast<int>(sizeof(QRgb));
    for (int y = 0; y < height; ++y) {
      auto &pixel = bits[y * stride + x];
      const auto mix = [alpha](int top, int under) {
        return static_cast<int>(std::lround(static_cast<float>(top) * alpha +
                                            static_cast<float>(under) * (1.0F - alpha)));
      };
      pixel = qRgb(mix(color.red, qRed(pixel)), mix(color.green, qGreen(pixel)),
                   mix(color.blue, qBlue(pixel)));
    }
  }
  return QPixmap::fromImage(image);
}

bool gradient_definitions_draw_same(const GradientDefinition &left,
                                    const GradientDefinition &right,
                                    const GradientPreviewColors &colors) {
  if (left.form != right.form || left.smoothness != right.smoothness)
    return false;
  if (left.form == GradientDefinitionForm::Noise)
    return left.noise == right.noise;
  const auto a = resolve_gradient_definition(left, colors.foreground, colors.background);
  const auto b = resolve_gradient_definition(right, colors.foreground, colors.background);
  // PSD and GRD store locations in 4096ths and midpoints and opacities in whole
  // or fractional percents, so a round trip moves them slightly.
  const auto near = [](float x, float y, float tolerance) {
    return std::abs(x - y) <= tolerance;
  };
  if (a.color_stops.size() != b.color_stops.size() ||
      a.alpha_stops.size() != b.alpha_stops.size())
    return false;
  for (std::size_t index = 0; index < a.color_stops.size(); ++index) {
    const auto &x = a.color_stops[index];
    const auto &y = b.color_stops[index];
    if (x.color != y.color || !near(x.location, y.location, 0.001F) ||
        (index > 0 && !near(x.midpoint, y.midpoint, 0.006F)))
      return false;
  }
  for (std::size_t index = 0; index < a.alpha_stops.size(); ++index) {
    const auto &x = a.alpha_stops[index];
    const auto &y = b.alpha_stops[index];
    if (!near(x.opacity, y.opacity, 0.006F) || !near(x.location, y.location, 0.001F) ||
        (index > 0 && !near(x.midpoint, y.midpoint, 0.006F)))
      return false;
  }
  return true;
}

QString matching_gradient_library_entry(const GradientLibrary &library,
                                        const GradientDefinition &gradient,
                                        const GradientPreviewColors &colors) {
  if (gradient.form == GradientDefinitionForm::Solid && gradient.color_stops.empty())
    return {};
  const GradientLibraryEntry *best = nullptr;
  int best_rank = -1;
  const auto name = QString::fromStdString(gradient.name);
  for (const auto &entry : library.entries()) {
    if (!gradient_definitions_draw_same(entry.definition, gradient, colors))
      continue;
    const int rank =
        (entry.name == name || QString::fromStdString(entry.definition.name) == name ? 2 : 0) +
        (gradient_has_dynamic_stops(entry.definition) ? 1 : 0);
    if (rank > best_rank) {
      best = &entry;
      best_rank = rank;
    }
  }
  return best != nullptr ? best->storage_id : QString();
}

QString gradient_library_entry_display_name(const GradientLibraryEntry &entry) {
  if (const auto *preset =
          find_builtin_gradient_preset(entry.storage_id.toStdString());
      preset != nullptr &&
      entry.name == QString::fromLatin1(preset->english_name))
    return translate_data_text(preset->english_name);
  return entry.name;
}

QString gradient_folder_display_name(const QString &folder) {
  for (const auto &preset : builtin_gradient_presets()) {
    if (folder == QString::fromLatin1(preset.english_folder))
      return translate_data_text(preset.english_folder);
  }
  return folder;
}

GradientLibrary::GradientLibrary(QString storage_dir, QObject *parent)
    : GradientLibraryBase(std::move(storage_dir), parent) {
  reload();
}

QPixmap GradientLibrary::thumbnail_for(const GradientDefinition &definition) const {
  return gradient_thumbnail(definition, 112, 28, preview_colors_);
}

void GradientLibrary::set_preview_colors(const GradientPreviewColors &colors) {
  if (colors == preview_colors_)
    return;
  preview_colors_ = colors;
  for (auto &entry : entries_)
    if (gradient_has_dynamic_stops(entry.definition))
      entry.thumbnail = thumbnail_for(entry.definition);
}

QString GradientLibrary::grd_path(const QString &id) const {
  return storage_path(id, ".grd");
}

void GradientLibrary::reload() {
  entries_.clear();
  const QDir dir(storage_dir_);
  for (const auto &file :
       dir.entryInfoList({QStringLiteral("*.grd")}, QDir::Files, QDir::Name)) {
    QFile input(file.absoluteFilePath());
    if (!input.open(QIODevice::ReadOnly))
      continue;
    const auto raw = input.readAll();
    std::string error;
    const auto parsed = patchy::psd::read_grd(
        std::span<const std::uint8_t>(
            reinterpret_cast<const std::uint8_t *>(raw.constData()),
            raw.size()),
        error);
    if (!parsed || parsed->gradients.empty())
      continue;
    GradientLibraryEntry entry;
    entry.storage_id = file.completeBaseName();
    entry.name = QString::fromUtf8(parsed->gradients.front().name);
    entry.folder = QString::fromUtf8(parsed->gradients.front().folder);
    entry.definition = parsed->gradients.front().definition;
    QFile sidecar(json_path(entry.storage_id));
    if (sidecar.open(QIODevice::ReadOnly)) {
      const auto object = QJsonDocument::fromJson(sidecar.readAll()).object();
      const auto name =
          object.value(QStringLiteral("name")).toString().trimmed();
      if (!name.isEmpty())
        entry.name = name;
      entry.folder =
          object.value(QStringLiteral("folder")).toString().trimmed();
    }
    entry.thumbnail = thumbnail_for(entry.definition);
    entries_.push_back(std::move(entry));
  }
  sort_entries();
}

bool GradientLibrary::write_sidecar(const GradientLibraryEntry &entry) const {
  // Persisted contract: the gradient sidecar always writes BOTH keys, folder
  // included when empty (unlike the other libraries' omit-when-empty shape).
  QJsonObject object;
  object.insert(QStringLiteral("name"), entry.name);
  object.insert(QStringLiteral("folder"), entry.folder);
  const auto bytes = QJsonDocument(object).toJson(QJsonDocument::Indented);
  QSaveFile file(json_path(entry.storage_id));
  return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() &&
         file.commit();
}

bool GradientLibrary::save_entry(const GradientLibraryEntry &entry) const {
  if (!QDir().mkpath(storage_dir_))
    return false;
  const auto bytes = bytes_for(entry.name, entry.folder, entry.definition);
  QSaveFile file(grd_path(entry.storage_id));
  if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() ||
      !file.commit())
    return false;
  return write_sidecar(entry);
}

QString GradientLibrary::add_gradient(const QString &name,
                                      const GradientDefinition &definition,
                                      const QString &folder,
                                      const QString &preferred_storage_id) {
  auto id = preferred_storage_id;
  if (id.isEmpty())
    id = QUuid::createUuid().toString(QUuid::WithoutBraces);
  if (find_entry(id) != nullptr)
    return {};
  GradientLibraryEntry entry{
      id, name.trimmed().isEmpty() ? tr("Untitled Gradient") : name.trimmed(),
      folder.trimmed(), definition, thumbnail_for(definition)};
  if (!save_entry(entry))
    return {};
  entries_.push_back(std::move(entry));
  sort_entries();
  emit changed();
  return id;
}

QString GradientLibrary::duplicate_gradient(const QString &id) {
  const auto *entry = find_entry(id);
  if (!entry)
    return {};
  return add_gradient(tr("%1 Copy").arg(entry->name), entry->definition,
                      entry->folder);
}

bool GradientLibrary::rename_gradient(const QString &id, const QString &name) {
  // Gradient edits rewrite the .grd payload too (the name/folder live inside
  // it), and a no-op rename still rewrites; both preserved from before the
  // shared skeleton.
  return rename_entry(id, name, /*skip_unchanged=*/false,
                      [this](const GradientLibraryEntry &entry) {
                        return save_entry(entry);
                      });
}

bool GradientLibrary::set_gradient_folder(const QString &id,
                                          const QString &folder) {
  return set_entry_folder(id, folder, /*skip_unchanged=*/false,
                          [this](const GradientLibraryEntry &entry) {
                            return save_entry(entry);
                          });
}

bool GradientLibrary::remove_entry_files(const QString &id) {
  QFile::remove(grd_path(id));
  QFile::remove(json_path(id));
  return true;
}

bool GradientLibrary::remove_gradient(const QString &id) {
  return remove_entry(
      id, [this](const QString &storage_id) { return remove_entry_files(storage_id); });
}

int GradientLibrary::remove_gradients(const QStringList &ids) {
  return remove_entries(
      ids, [this](const QString &storage_id) { return remove_entry_files(storage_id); });
}

QString GradientLibrary::import_grd(const QString &path, QString &error,
                                    QStringList &warnings) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    error = tr("Could not read the GRD file.");
    return {};
  }
  const auto raw = file.readAll();
  std::string parse_error;
  const auto parsed = patchy::psd::read_grd(
      std::span<const std::uint8_t>(
          reinterpret_cast<const std::uint8_t *>(raw.constData()), raw.size()),
      parse_error);
  if (!parsed) {
    error = translate_data_text(parse_error);
    return {};
  }
  for (const auto &warning : parsed->warnings)
    warnings.push_back(translate_data_text(warning));
  const auto fallback_folder = QFileInfo(path).completeBaseName();
  QString first;
  for (const auto &item : parsed->gradients) {
    const auto name = QString::fromUtf8(item.name);
    const auto folder =
        item.folder.empty() ? fallback_folder : QString::fromUtf8(item.folder);
    const auto duplicate = std::find_if(
        entries_.begin(), entries_.end(), [&](const auto &existing) {
          return existing.name == name &&
                 definitions_equal(existing.definition, item.definition);
        });
    const auto id = duplicate != entries_.end()
                        ? duplicate->storage_id
                        : add_gradient(name, item.definition, folder);
    if (first.isEmpty())
      first = id;
  }
  error.clear();
  return first;
}

bool GradientLibrary::export_grd(const QStringList &ids, const QString &path,
                                 QString &error) const {
  std::vector<patchy::psd::GrdGradient> gradients;
  for (const auto &id : ids)
    if (const auto *entry = find_entry(id))
      gradients.push_back({entry->name.toUtf8().toStdString(),
                           entry->folder.toUtf8().toStdString(),
                           entry->definition});
  if (gradients.empty()) {
    error = tr("Select at least one gradient to export.");
    return false;
  }
  const auto bytes = patchy::psd::write_grd(gradients);
  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly) ||
      file.write(reinterpret_cast<const char *>(bytes.data()), bytes.size()) !=
          static_cast<qint64>(bytes.size()) ||
      !file.commit()) {
    error = tr("Could not write the GRD file.");
    return false;
  }
  error.clear();
  return true;
}

int GradientLibrary::restore_default_gradients(int newer_than_version) {
  int added = 0;
  for (const auto &preset : builtin_gradient_presets()) {
    if (preset.introduced_version <= newer_than_version ||
        find_entry(QString::fromLatin1(preset.id)))
      continue;
    if (!add_gradient(QString::fromLatin1(preset.english_name),
                      preset.definition,
                      canonical_folder(preset.english_folder),
                      QString::fromLatin1(preset.id))
             .isEmpty())
      ++added;
  }
  return added;
}

int GradientLibrary::reset_default_gradients_to_factory() {
  int reset = 0;
  for (const auto &preset : builtin_gradient_presets()) {
    auto *entry = const_cast<GradientLibraryEntry *>(
        find_entry(QString::fromLatin1(preset.id)));
    if (!entry)
      continue;
    const auto name = QString::fromLatin1(preset.english_name);
    const auto folder = canonical_folder(preset.english_folder);
    if (entry->name == name && entry->folder == folder &&
        definitions_equal(entry->definition, preset.definition))
      continue;
    auto updated = *entry;
    updated.name = name;
    updated.folder = folder;
    updated.definition = preset.definition;
    updated.thumbnail = thumbnail_for(updated.definition);
    if (save_entry(updated)) {
      *entry = std::move(updated);
      ++reset;
    }
  }
  if (reset) {
    sort_entries();
    emit changed();
  }
  return reset;
}

bool GradientLibrary::has_all_default_gradients_introduced_after(
    int version) const {
  return std::all_of(builtin_gradient_presets().begin(),
                     builtin_gradient_presets().end(), [&](const auto &preset) {
                       return preset.introduced_version <= version ||
                              find_entry(QString::fromLatin1(preset.id)) !=
                                  nullptr;
                     });
}

bool GradientLibrary::default_gradients_match_factory() const {
  return std::all_of(
      builtin_gradient_presets().begin(), builtin_gradient_presets().end(),
      [&](const auto &preset) {
        const auto *entry = find_entry(QString::fromLatin1(preset.id));
        return entry &&
               entry->name == QString::fromLatin1(preset.english_name) &&
               entry->folder == canonical_folder(preset.english_folder) &&
               definitions_equal(entry->definition, preset.definition);
      });
}

} // namespace patchy::ui
