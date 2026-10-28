#include "ui/custom_shape_library.hpp"

#include "core/document.hpp"
#include "formats/svg_document_io.hpp"
#include "psd/csh_io.hpp"
#include "ui/default_custom_shapes.hpp"
#include "ui/localization.hpp"

#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QPainterPath>
#include <QSaveFile>
#include <QSignalBlocker>
#include <QUuid>

#include <algorithm>
#include <cstdint>
#include <exception>
#include <span>
#include <utility>

namespace patchy::ui {

namespace {

const BuiltinCustomShape* builtin_shape(const QString& shape_id) {
  for (const auto& builtin : builtin_custom_shapes()) {
    if (shape_id == QLatin1String(builtin.id)) {
      return &builtin;
    }
  }
  return nullptr;
}

}  // namespace

QString custom_shape_folder_display_name(const QString& folder) {
  for (const auto& builtin : builtin_custom_shapes()) {
    if (folder == QLatin1String(builtin.english_folder)) {
      return translate_data_text(builtin.english_folder);
    }
  }
  return folder;
}

bool normalize_custom_shape_path(VectorPath& path) {
  const auto bounds = path.bounds();
  if (!bounds.has_value() || bounds->right - bounds->left < 1e-6 || bounds->bottom - bounds->top < 1e-6) {
    return false;
  }
  const auto scale = 1.0 / std::max(bounds->right - bounds->left, bounds->bottom - bounds->top);
  transform_vector_path(path, {scale, 0.0, 0.0, scale, -bounds->left * scale, -bounds->top * scale});
  return true;
}

std::optional<VectorPath> custom_shape_path_from_svg_file(const QString& path, QString& error) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    error = QCoreApplication::translate("patchy::ui::CustomShapeLibrary", "Could not read %1")
                .arg(QFileInfo(path).fileName());
    return std::nullopt;
  }
  const auto raw = file.readAll();
  Document imported;
  try {
    imported = svg::DocumentIo::read(
        std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(raw.constData()),
                                      static_cast<std::size_t>(raw.size())));
  } catch (const std::exception& failure) {
    error = QCoreApplication::translate("patchy::ui::CustomShapeLibrary", "Could not read the SVG: %1")
                .arg(translate_data_text(failure.what()));
    return std::nullopt;
  }
  // The Photoshop Shapes-panel behavior: one stampable shape per file, all
  // drawable geometry merged (paint ignored), combine ops preserved so holes
  // keep cutting when stamped. Shape groups renumber per source layer.
  VectorPath merged;
  std::int32_t group_base = 0;
  const auto collect = [&](auto&& self, const std::vector<Layer>& layers) -> void {
    for (const auto& layer : layers) {
      if (!layer.children().empty()) {
        self(self, layer.children());
      }
      const auto* shape = layer.vector_shape();
      if (shape == nullptr || shape->path.subpaths.empty()) {
        continue;
      }
      std::int32_t highest = 0;
      for (const auto& subpath : shape->path.subpaths) {
        auto copy = subpath;
        highest = std::max(highest, copy.shape_group);
        copy.shape_group += group_base;
        merged.subpaths.push_back(std::move(copy));
      }
      group_base += highest + 1;
    }
  };
  collect(collect, std::as_const(imported).layers());
  if (!normalize_custom_shape_path(merged)) {
    error = QCoreApplication::translate("patchy::ui::CustomShapeLibrary", "The SVG has no shape geometry to define");
    return std::nullopt;
  }
  return merged;
}

QPixmap custom_shape_thumbnail(const VectorPath& path, int size) {
  QPixmap pixmap(size, size);
  pixmap.fill(QColor(52, 56, 64));
  QPainter painter(&pixmap);
  painter.setRenderHint(QPainter::Antialiasing, true);
  const double margin = size * 0.12;
  const double extent = size - margin * 2.0;
  QPainterPath outline;
  for (const auto& subpath : path.subpaths) {
    if (subpath.anchors.empty()) {
      continue;
    }
    const auto to_point = [margin, extent](double x, double y) {
      return QPointF(margin + x * extent, margin + y * extent);
    };
    outline.moveTo(to_point(subpath.anchors[0].anchor_x, subpath.anchors[0].anchor_y));
    const auto anchor_count = subpath.anchors.size();
    const auto segment_count = subpath.closed ? anchor_count : anchor_count - 1;
    for (std::size_t i = 0; i < segment_count; ++i) {
      const auto& a = subpath.anchors[i];
      const auto& b = subpath.anchors[(i + 1) % anchor_count];
      outline.cubicTo(to_point(a.out_x, a.out_y), to_point(b.in_x, b.in_y),
                      to_point(b.anchor_x, b.anchor_y));
    }
    outline.closeSubpath();
  }
  outline.setFillRule(Qt::OddEvenFill);
  painter.setPen(Qt::NoPen);
  painter.setBrush(QColor(220, 226, 235));
  painter.drawPath(outline);
  painter.setPen(QPen(QColor(150, 158, 168), 1));
  painter.setBrush(Qt::NoBrush);
  painter.drawRect(QRect(0, 0, size - 1, size - 1));
  return pixmap;
}

QString custom_shape_display_name(const CustomShapeLibraryEntry& entry) {
  for (const auto& builtin : builtin_custom_shapes()) {
    if (entry.id == QLatin1String(builtin.id) &&
        entry.name == QLatin1String(builtin.english_name)) {
      return translate_data_text(builtin.english_name);
    }
  }
  return entry.name;
}

void populate_custom_shape_combo(QComboBox& combo, const CustomShapeLibrary& library) {
  const auto previous = combo.currentData().toString();
  QSignalBlocker blocker(&combo);
  combo.setIconSize(QSize(24, 24));
  combo.clear();
  for (const auto& entry : library.entries()) {
    combo.addItem(QIcon(entry.thumbnail), custom_shape_display_name(entry), entry.id);
  }
  if (const auto index = combo.findData(previous); index >= 0) {
    combo.setCurrentIndex(index);
  }
}

CustomShapeLibrary::CustomShapeLibrary(QString storage_dir, QObject* parent)
    : CustomShapeLibraryBase(std::move(storage_dir), parent) {
  reload();
}

void CustomShapeLibrary::reload() {
  entries_.clear();
  const QDir dir(storage_dir_);
  for (const auto& file :
       dir.entryInfoList({QStringLiteral("*.json")}, QDir::Files, QDir::Name)) {
    QFile input(file.absoluteFilePath());
    if (!input.open(QIODevice::ReadOnly)) {
      continue;
    }
    const auto object = QJsonDocument::fromJson(input.readAll()).object();
    const auto path_text = object.value(QStringLiteral("path")).toString();
    auto parsed = parse_vector_path(path_text.toStdString());
    if (!parsed.has_value() || parsed->empty()) {
      continue;
    }
    CustomShapeLibraryEntry entry;
    entry.storage_id = file.completeBaseName();
    entry.id = object.value(QStringLiteral("id")).toString();
    entry.name = object.value(QStringLiteral("name")).toString();
    entry.folder = object.value(QStringLiteral("folder")).toString();
    entry.path = std::move(*parsed);
    entry.thumbnail = custom_shape_thumbnail(entry.path);
    if (entry.id.isEmpty() || entry.name.isEmpty()) {
      continue;
    }
    entries_.push_back(std::move(entry));
  }
  sort_entries();
}

bool CustomShapeLibrary::write_sidecar(const CustomShapeLibraryEntry& entry) const {
  if (!QDir().mkpath(storage_dir_)) {
    return false;
  }
  QJsonObject object;
  object.insert(QStringLiteral("id"), entry.id);
  object.insert(QStringLiteral("name"), entry.name);
  object.insert(QStringLiteral("folder"), entry.folder);
  object.insert(QStringLiteral("path"),
                QString::fromStdString(serialize_vector_path(entry.path)));
  QSaveFile file(json_path(entry.storage_id));
  if (!file.open(QIODevice::WriteOnly)) {
    return false;
  }
  file.write(QJsonDocument(object).toJson(QJsonDocument::Indented));
  return file.commit();
}

QString CustomShapeLibrary::add_shape(const QString& name, const VectorPath& path,
                                      const QString& folder, const QString& preferred_storage_id,
                                      const QString& shape_id) {
  auto storage_id = add_shape_without_signal(name, path, folder, preferred_storage_id, shape_id);
  if (!storage_id.isEmpty()) {
    emit changed();
  }
  return storage_id;
}

QString CustomShapeLibrary::add_shape_without_signal(const QString& name, const VectorPath& path,
                                                     const QString& folder, const QString& preferred_storage_id,
                                                     const QString& shape_id) {
  if (path.empty()) {
    return {};
  }
  auto storage_id = preferred_storage_id;
  if (storage_id.isEmpty() || find_entry(storage_id) != nullptr) {
    storage_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
  }
  CustomShapeLibraryEntry entry;
  entry.storage_id = storage_id;
  entry.id = shape_id.isEmpty()
                 ? QStringLiteral("shape.user.%1")
                       .arg(QUuid::createUuid().toString(QUuid::WithoutBraces))
                 : shape_id;
  entry.name = name;
  entry.folder = folder;
  entry.path = path;
  entry.thumbnail = custom_shape_thumbnail(entry.path);
  if (!write_sidecar(entry)) {
    return {};
  }
  entries_.push_back(std::move(entry));
  sort_entries();
  return storage_id;
}

QStringList CustomShapeLibrary::import_csh(const QString& path, QString& error, QStringList& warnings) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    error = tr("Could not read %1").arg(QFileInfo(path).fileName());
    return {};
  }
  const auto raw = file.readAll();
  std::string read_error;
  const auto parsed = psd::read_csh(
      std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(raw.constData()),
                                    static_cast<std::size_t>(raw.size())),
      read_error);
  if (!parsed.has_value()) {
    error = translate_data_text(read_error);
    return {};
  }
  for (const auto& warning : parsed->warnings) {
    warnings.append(translate_data_text(warning));
  }
  const auto folder = QFileInfo(path).completeBaseName();
  QStringList added;
  int number = 0;
  for (const auto& shape : parsed->shapes) {
    ++number;
    auto name = QString::fromStdString(shape.name).trimmed();
    if (name.isEmpty()) {
      name = tr("Shape %1").arg(number);
    }
    const auto storage_id = add_shape_without_signal(name, shape.path, folder, {}, {});
    if (storage_id.isEmpty()) {
      error = tr("Could not save the custom shape");
      break;
    }
    added.append(storage_id);
  }
  if (!added.isEmpty()) {
    error.clear();
    emit changed();
  }
  return added;
}

bool CustomShapeLibrary::rename_shape(const QString& storage_id, const QString& name) {
  return rename_entry(storage_id, name, /*skip_unchanged=*/true,
                      [this](const CustomShapeLibraryEntry& entry) { return write_sidecar(entry); });
}

bool CustomShapeLibrary::set_shape_folder(const QString& storage_id, const QString& folder) {
  return set_entry_folder(storage_id, folder, /*skip_unchanged=*/true,
                          [this](const CustomShapeLibraryEntry& entry) { return write_sidecar(entry); });
}

int CustomShapeLibrary::move_shapes_to_folder(const QStringList& storage_ids, const QString& folder) {
  const auto target = folder.trimmed();
  int moved = 0;
  for (auto& entry : entries_) {
    if (!storage_ids.contains(entry.storage_id) || entry.folder == target) {
      continue;
    }
    auto updated = entry;
    updated.folder = target;
    if (write_sidecar(updated)) {
      entry.folder = target;
      ++moved;
    }
  }
  if (moved > 0) {
    sort_entries();
    emit changed();
  }
  return moved;
}

int CustomShapeLibrary::rename_folder(const QString& from, const QString& to) {
  const auto target = to.trimmed();
  if (from.isEmpty() || target.isEmpty() || target == from) {
    return 0;
  }
  QStringList ids;
  for (const auto& entry : entries_) {
    if (entry.folder == from) {
      ids.append(entry.storage_id);
    }
  }
  return move_shapes_to_folder(ids, target);
}

bool CustomShapeLibrary::remove_shape(const QString& storage_id) {
  return remove_shapes({storage_id}) == 1;
}

int CustomShapeLibrary::remove_shapes(const QStringList& storage_ids) {
  return remove_entries(storage_ids, [this](const QString& storage_id) {
    const auto file = json_path(storage_id);
    return !QFile::exists(file) || QFile::remove(file);
  });
}

QString CustomShapeLibrary::duplicate_shape(const QString& storage_id) {
  const auto* entry = find_entry(storage_id);
  if (entry == nullptr) {
    return {};
  }
  // Copies first: add_shape reallocates the entry vector.
  const auto name = tr("%1 Copy").arg(custom_shape_display_name(*entry));
  const auto path = entry->path;
  const auto folder = entry->folder;
  return add_shape(name, path, folder);
}

const CustomShapeLibraryEntry* CustomShapeLibrary::find_entry_by_shape_id(
    const QString& shape_id) const {
  return find_entry_if(
      [&shape_id](const CustomShapeLibraryEntry& entry) { return entry.id == shape_id; });
}

int CustomShapeLibrary::restore_default_shapes(int newer_than_version) {
  int added = 0;
  bool refreshed = false;
  for (const auto& builtin : builtin_custom_shapes()) {
    if (const auto* existing = find_entry_by_shape_id(QLatin1String(builtin.id))) {
      // Builtin geometry is code-authoritative: a sidecar materialized by an
      // older build picks up path changes here (user renames are kept; the
      // app never edits a builtin's geometry in place, so a difference always
      // means the code moved on).
      if (existing->path != builtin.path) {
        const auto it = entry_iterator(existing->storage_id);
        it->path = builtin.path;
        it->thumbnail = custom_shape_thumbnail(it->path);
        write_sidecar(*it);
        refreshed = true;
      }
      continue;
    }
    if (builtin.introduced_version <= newer_than_version) {
      continue;  // a deleted default stays deleted
    }
    // The storage id doubles as the shape id for builtins (stable filenames).
    const auto storage_id = QString::fromLatin1(builtin.id);
    if (!add_shape(QLatin1String(builtin.english_name), builtin.path,
                   QLatin1String(builtin.english_folder), storage_id,
                   QLatin1String(builtin.id))
             .isEmpty()) {
      ++added;
    }
  }
  if (refreshed) {
    emit changed();
  }
  return added;
}

int CustomShapeLibrary::reset_default_shapes_to_factory() {
  int reset = 0;
  for (auto& entry : entries_) {
    const auto* builtin = builtin_shape(entry.id);
    if (builtin == nullptr) {
      continue;
    }
    const auto name = QString::fromLatin1(builtin->english_name);
    const auto folder = QString::fromLatin1(builtin->english_folder);
    if (entry.name == name && entry.folder == folder && entry.path == builtin->path) {
      continue;
    }
    auto updated = entry;
    updated.name = name;
    updated.folder = folder;
    updated.path = builtin->path;
    updated.thumbnail = custom_shape_thumbnail(updated.path);
    if (write_sidecar(updated)) {
      entry = std::move(updated);
      ++reset;
    }
  }
  if (reset > 0) {
    sort_entries();
    emit changed();
  }
  return reset;
}

bool CustomShapeLibrary::has_all_default_shapes_introduced_after(int version) const {
  const auto& builtins = builtin_custom_shapes();
  return std::all_of(builtins.begin(), builtins.end(), [this, version](const BuiltinCustomShape& builtin) {
    return builtin.introduced_version <= version ||
           find_entry_by_shape_id(QLatin1String(builtin.id)) != nullptr;
  });
}

bool CustomShapeLibrary::default_shapes_match_factory() const {
  for (const auto& builtin : builtin_custom_shapes()) {
    const auto* entry = find_entry_by_shape_id(QLatin1String(builtin.id));
    if (entry == nullptr || entry->name != QLatin1String(builtin.english_name) ||
        entry->folder != QLatin1String(builtin.english_folder) || entry->path != builtin.path) {
      return false;
    }
  }
  return true;
}

}  // namespace patchy::ui
