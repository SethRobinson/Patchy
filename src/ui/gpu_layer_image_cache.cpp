#include "ui/gpu_layer_image_cache.hpp"

#include "ui/edit_conversions.hpp"

#include <QSize>

#include <algorithm>

namespace patchy::ui {

QImage GpuLayerImageCache::layer_image(std::uint64_t layer_id, std::uint64_t pixel_revision,
                                       const PixelBuffer& pixels) {
  auto& entry = entries_[layer_id];
  const QSize size(pixels.width(), pixels.height());
  if (!entry.has_image || entry.pixel_revision != pixel_revision || entry.image.size() != size) {
    entry.image = qimage_from_pixel_buffer(pixels);
    entry.pixel_revision = pixel_revision;
    entry.has_image = true;
    ++conversions_;
  }
  return entry.image;
}

QImage GpuLayerImageCache::mask_image(std::uint64_t layer_id, std::uint64_t mask_revision,
                                      const PixelBuffer& pixels) {
  auto& entry = entries_[layer_id];
  const QSize size(pixels.width(), pixels.height());
  if (!entry.has_mask || entry.mask_revision != mask_revision || entry.mask.size() != size) {
    entry.mask = grayscale_qimage_from_pixel_buffer(pixels);
    entry.mask_revision = mask_revision;
    entry.has_mask = true;
    ++conversions_;
  }
  return entry.mask;
}

void GpuLayerImageCache::retain_only(const std::vector<std::uint64_t>& layer_ids) {
  for (auto it = entries_.begin(); it != entries_.end();) {
    if (std::find(layer_ids.begin(), layer_ids.end(), it->first) == layer_ids.end()) {
      it = entries_.erase(it);
    } else {
      ++it;
    }
  }
}

void GpuLayerImageCache::clear() noexcept {
  entries_.clear();
}

}  // namespace patchy::ui
