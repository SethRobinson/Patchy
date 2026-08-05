#include "core/document_memory.hpp"

#include "core/document.hpp"
#include "core/layer.hpp"
#include "core/pixel_buffer.hpp"
#include "core/smart_filter.hpp"

namespace patchy {

namespace {

void visit_layer_buffers(const Layer& layer,
                         const std::function<void(const PixelBuffer&)>& visit) {
  visit(layer.pixels());
  if (layer.mask().has_value()) {
    visit(layer.mask()->pixels);
  }
  if (const auto* stack = layer.smart_filter_stack(); stack != nullptr) {
    visit(stack->mask.pixels);
  }
  for (const auto& child : layer.children()) {
    visit_layer_buffers(child, visit);
  }
}

// The identity of a buffer's shared copy-on-write storage. Only meaningful for
// non-empty buffers: every empty buffer aliases one shared sentinel vector.
[[nodiscard]] const void* storage_identity(const PixelBuffer& buffer) noexcept {
  return buffer.data().data();
}

}  // namespace

void visit_pixel_buffers(const Document& document,
                         const std::function<void(const PixelBuffer&)>& visit) {
  for (const auto& layer : document.layers()) {
    visit_layer_buffers(layer, visit);
  }
  for (const auto& channel : document.channels()) {
    visit(channel.pixels());
  }
  if (document.metadata().psd_flat_composite.has_value()) {
    visit(*document.metadata().psd_flat_composite);
  }
}

void collect_pixel_storage(const Document& document, PixelStorageSet& out) {
  visit_pixel_buffers(document, [&out](const PixelBuffer& buffer) {
    if (buffer.byte_size() == 0) {
      return;
    }
    out.insert(storage_identity(buffer));
  });
}

std::size_t accumulate_unique_pixel_bytes(const Document& document,
                                          const PixelStorageSet& exclude,
                                          PixelStorageSet& seen) {
  std::size_t total = 0;
  visit_pixel_buffers(document, [&](const PixelBuffer& buffer) {
    if (buffer.byte_size() == 0) {
      return;
    }
    const void* identity = storage_identity(buffer);
    if (exclude.contains(identity)) {
      return;
    }
    if (seen.insert(identity).second) {
      total += buffer.byte_size();
    }
  });
  return total;
}

}  // namespace patchy
