#include "ui/custom_shape_library.hpp"

#include "ui/default_custom_shapes.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QPainterPath>
#include <QSaveFile>
#include <QUuid>

#include <utility>

namespace patchy::ui {

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
      return QCoreApplication::translate("QObject", builtin.english_name);
    }
  }
  return entry.name;
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
  emit changed();
  return storage_id;
}

bool CustomShapeLibrary::rename_shape(const QString& storage_id, const QString& name) {
  const auto it = entry_iterator(storage_id);
  if (it == entries_.end() || name.trimmed().isEmpty()) {
    return false;
  }
  it->name = name.trimmed();
  const auto ok = write_sidecar(*it);
  sort_entries();
  emit changed();
  return ok;
}

bool CustomShapeLibrary::remove_shape(const QString& storage_id) {
  const auto it = entry_iterator(storage_id);
  if (it == entries_.end()) {
    return false;
  }
  QFile::remove(json_path(storage_id));
  entries_.erase(it);
  emit changed();
  return true;
}

const CustomShapeLibraryEntry* CustomShapeLibrary::find_entry_by_shape_id(
    const QString& shape_id) const {
  return find_entry_if(
      [&shape_id](const CustomShapeLibraryEntry& entry) { return entry.id == shape_id; });
}

int CustomShapeLibrary::restore_default_shapes() {
  int added = 0;
  for (const auto& builtin : builtin_custom_shapes()) {
    if (find_entry_by_shape_id(QLatin1String(builtin.id)) != nullptr) {
      continue;
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
  return added;
}

}  // namespace patchy::ui
