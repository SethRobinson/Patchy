#include "ui/preset_library.hpp"

#include "core/pixel_buffer.hpp"
#include "ui/app_settings.hpp"

#include <QFileInfo>
#include <QImage>
#include <QSaveFile>

namespace patchy::ui {

PresetLibraryBase::PresetLibraryBase(QString storage_dir, const char* subdir, QObject* parent)
    : QObject(parent) {
  if (storage_dir.isEmpty()) {
    const auto settings = app_settings();
    storage_dir_ =
        QFileInfo(settings.fileName()).absolutePath() + QLatin1Char('/') + QLatin1String(subdir);
  } else {
    storage_dir_ = std::move(storage_dir);
  }
}

QString PresetLibraryBase::storage_path(const QString& id, const char* suffix) const {
  return storage_dir_ + QLatin1Char('/') + id + QLatin1String(suffix);
}

QString PresetLibraryBase::json_path(const QString& id) const {
  return storage_path(id, ".json");
}

}  // namespace patchy::ui

namespace patchy::ui::presets {

QString qstring_from_utf8(const std::string& text) {
  return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

std::string utf8_from_qstring(const QString& text) {
  const auto bytes = text.toUtf8();
  return std::string(bytes.constData(), static_cast<std::size_t>(bytes.size()));
}

bool pattern_tiles_equal(const PixelBuffer& lhs, const PixelBuffer& rhs) {
  return lhs.width() == rhs.width() && lhs.height() == rhs.height() &&
         lhs.format() == rhs.format() && lhs.data().size() == rhs.data().size() &&
         std::equal(lhs.data().begin(), lhs.data().end(), rhs.data().begin());
}

bool save_png(const QImage& image, const QString& path) {
  if (image.isNull()) {
    return false;
  }
  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly) || !image.save(&file, "PNG")) {
    return false;
  }
  return file.commit();
}

}  // namespace patchy::ui::presets
