#include "ui/photo_pattern_presets.hpp"

#include "core/pattern_presets.hpp"

#include <QImage>
#include <QString>

#include <cstring>

// Q_INIT_RESOURCE must run from outside any namespace. Referencing the
// generated initializer here also forces the linker to pull the resource
// object out of the static patchy_ui library (its self-registering global
// would otherwise be dead-stripped from executables that never name it).
static void ensure_texture_resources_registered() {
  static const bool registered = [] {
    Q_INIT_RESOURCE(textures);
    return true;
  }();
  (void)registered;
}

namespace patchy::ui {

std::optional<PatternResource> photo_pattern_resource(std::string_view id) {
  ensure_texture_resources_registered();
  const auto* preset = find_photo_pattern_preset(id);
  if (preset == nullptr) {
    return std::nullopt;
  }
  const auto image = QImage(QStringLiteral(":/patchy/textures/") +
                            QString::fromLatin1(preset->resource_alias))
                         .convertToFormat(QImage::Format_RGBA8888);
  if (image.isNull() || image.width() <= 0 || image.height() <= 0) {
    return std::nullopt;
  }
  PatternResource resource;
  resource.id = preset->id;
  resource.name = preset->english_name;
  resource.tile = PixelBuffer(image.width(), image.height(), PixelFormat::rgba8());
  const auto row_bytes = static_cast<std::size_t>(image.width()) * 4U;
  for (int y = 0; y < image.height(); ++y) {
    std::memcpy(resource.tile.row(y).data(), image.constScanLine(y), row_bytes);
  }
  resource.provenance = PatternProvenance::Authored;
  return resource;
}

bool is_bundled_pattern_id(std::string_view id) {
  return find_builtin_pattern_preset(id) != nullptr || find_photo_pattern_preset(id) != nullptr;
}

std::optional<PatternResource> bundled_pattern_resource(std::string_view id) {
  if (find_builtin_pattern_preset(id) != nullptr) {
    return builtin_pattern_resource(id);
  }
  return photo_pattern_resource(id);
}

}  // namespace patchy::ui
