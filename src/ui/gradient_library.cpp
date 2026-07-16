#include "ui/gradient_library.hpp"

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
#include <QSaveFile>
#include <QUuid>

#include <algorithm>

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

QPixmap gradient_thumbnail(const GradientDefinition &definition, int width,
                           int height) {
  width = std::max(16, width);
  height = std::max(8, height);
  QImage image(width, height, QImage::Format_RGB32);
  auto gradient = render_gradient(definition);
  for (int x = 0; x < width; ++x) {
    const auto position =
        width <= 1 ? 0.0F
                   : static_cast<float>(x) / static_cast<float>(width - 1);
    const auto color = gradient_color(gradient, position);
    const auto qcolor = QColor(color.red, color.green, color.blue);
    for (int y = 0; y < height; ++y)
      image.setPixelColor(x, y, qcolor);
  }
  return QPixmap::fromImage(image);
}

QString gradient_library_entry_display_name(const GradientLibraryEntry &entry) {
  if (const auto *preset =
          find_builtin_gradient_preset(entry.storage_id.toStdString());
      preset != nullptr &&
      entry.name == QString::fromLatin1(preset->english_name))
    return QCoreApplication::translate("QObject", preset->english_name);
  return entry.name;
}

QString gradient_folder_display_name(const QString &folder) {
  for (const auto &preset : builtin_gradient_presets()) {
    if (folder == QString::fromLatin1(preset.english_folder))
      return QCoreApplication::translate("QObject", preset.english_folder);
  }
  return folder;
}

GradientLibrary::GradientLibrary(QString storage_dir, QObject *parent)
    : GradientLibraryBase(std::move(storage_dir), parent) {
  reload();
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
    entry.thumbnail = gradient_thumbnail(entry.definition);
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
      folder.trimmed(), definition, gradient_thumbnail(definition)};
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
    error = QString::fromStdString(parse_error);
    return {};
  }
  for (const auto &warning : parsed->warnings)
    warnings.push_back(QString::fromStdString(warning));
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
    entry->name = name;
    entry->folder = folder;
    entry->definition = preset.definition;
    entry->thumbnail = gradient_thumbnail(entry->definition);
    if (save_entry(*entry))
      ++reset;
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
