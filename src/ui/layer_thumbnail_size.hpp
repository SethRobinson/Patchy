#pragma once

#include <QString>

namespace patchy::ui {

// The Layers panel's thumbnail size (GitHub issue 386), saved in QSettings under
// kLayerThumbnailSizeSettingsKey as one of the tokens below. The key and the tokens are
// persisted identifiers: never rename them.
enum class LayerThumbnailSize { None, Small, Medium, Large };

inline constexpr const char* kLayerThumbnailSizeSettingsKey = "view/layerThumbnailSize";

// Medium is the 28 px tile every row used before the setting existed.
inline constexpr LayerThumbnailSize kDefaultLayerThumbnailSize = LayerThumbnailSize::Medium;

// Content and mask tiles share this edge. None shows no content tile but keeps masks at the
// Small size: the mask tile is where a click targets the mask for painting.
[[nodiscard]] constexpr int layer_thumbnail_tile_size(LayerThumbnailSize size) {
  switch (size) {
    case LayerThumbnailSize::None:
      return 0;
    case LayerThumbnailSize::Small:
      return 20;
    case LayerThumbnailSize::Medium:
      return 28;
    case LayerThumbnailSize::Large:
      return 48;
  }
  return 28;
}

[[nodiscard]] constexpr int layer_mask_thumbnail_tile_size(LayerThumbnailSize size) {
  return size == LayerThumbnailSize::None ? layer_thumbnail_tile_size(LayerThumbnailSize::Small)
                                          : layer_thumbnail_tile_size(size);
}

// The main row's height; Smart Filter and effect rows below it stay 26 px. None and Small keep
// room for the name and details lines.
[[nodiscard]] constexpr int layer_main_row_height(LayerThumbnailSize size) {
  switch (size) {
    case LayerThumbnailSize::None:
    case LayerThumbnailSize::Small:
      return 36;
    case LayerThumbnailSize::Medium:
      return 44;
    case LayerThumbnailSize::Large:
      return 60;
  }
  return 44;
}

[[nodiscard]] inline QString layer_thumbnail_size_token(LayerThumbnailSize size) {
  switch (size) {
    case LayerThumbnailSize::None:
      return QStringLiteral("none");
    case LayerThumbnailSize::Small:
      return QStringLiteral("small");
    case LayerThumbnailSize::Medium:
      return QStringLiteral("medium");
    case LayerThumbnailSize::Large:
      return QStringLiteral("large");
  }
  return QStringLiteral("medium");
}

[[nodiscard]] inline LayerThumbnailSize layer_thumbnail_size_from_token(const QString& token) {
  if (token == QStringLiteral("none")) {
    return LayerThumbnailSize::None;
  }
  if (token == QStringLiteral("small")) {
    return LayerThumbnailSize::Small;
  }
  if (token == QStringLiteral("large")) {
    return LayerThumbnailSize::Large;
  }
  return kDefaultLayerThumbnailSize;
}

}  // namespace patchy::ui
