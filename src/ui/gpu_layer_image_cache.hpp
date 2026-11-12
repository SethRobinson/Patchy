#pragma once

#include "core/pixel_buffer.hpp"

#include <QImage>

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace patchy::ui {

// Revision-keyed QImage views of layer pixel and mask buffers for the GPU
// document snapshot.
//
// build_gpu_document runs on every repaint request. Converting every visible
// layer's PixelBuffer into a fresh QImage each time is O(layer pixels) per
// repaint even when nothing changed, which docs/performance.md forbids. The
// cache returns the previous (implicitly shared) QImage while the layer's
// pixel or mask revision and buffer size are unchanged, so an unchanged
// repaint costs a hash lookup per layer instead of a copy.
class GpuLayerImageCache {
public:
  [[nodiscard]] QImage layer_image(std::uint64_t layer_id, std::uint64_t pixel_revision,
                                   const PixelBuffer& pixels);
  [[nodiscard]] QImage mask_image(std::uint64_t layer_id, std::uint64_t mask_revision,
                                  const PixelBuffer& pixels);

  // Drops entries for layers that are no longer part of the snapshot.
  void retain_only(const std::vector<std::uint64_t>& layer_ids);
  void clear() noexcept;

  [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }
  // Number of buffer-to-image conversions performed so far (for tests).
  [[nodiscard]] std::size_t conversions() const noexcept { return conversions_; }

private:
  struct Entry {
    bool has_image{false};
    std::uint64_t pixel_revision{0};
    QImage image;
    bool has_mask{false};
    std::uint64_t mask_revision{0};
    QImage mask;
  };
  std::unordered_map<std::uint64_t, Entry> entries_;
  std::size_t conversions_{0};
};

}  // namespace patchy::ui
