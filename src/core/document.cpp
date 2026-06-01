#include "core/document.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace patchy {

Document::Document(std::int32_t width, std::int32_t height, PixelFormat format)
    : width_(width), height_(height), format_(format) {
  if (width < 0 || height < 0) {
    throw std::invalid_argument("Document dimensions cannot be negative");
  }
  color_state_.working_mode = format.color_mode;
  color_state_.bit_depth = format.bit_depth;
}

std::int32_t Document::width() const noexcept {
  return width_;
}

std::int32_t Document::height() const noexcept {
  return height_;
}

PixelFormat Document::format() const noexcept {
  return format_;
}

const DocumentColorState& Document::color_state() const noexcept {
  return color_state_;
}

DocumentColorState& Document::color_state() noexcept {
  return color_state_;
}

const DocumentMetadata& Document::metadata() const noexcept {
  return metadata_;
}

DocumentMetadata& Document::metadata() noexcept {
  return metadata_;
}

const DocumentPrintSettings& Document::print_settings() const noexcept {
  return print_settings_;
}

DocumentPrintSettings& Document::print_settings() noexcept {
  return print_settings_;
}

const std::optional<DocumentIndexedPalette>& Document::indexed_palette() const noexcept {
  return indexed_palette_;
}

std::optional<DocumentIndexedPalette>& Document::indexed_palette() noexcept {
  return indexed_palette_;
}

const DocumentGridSettings& Document::grid_settings() const noexcept {
  return grid_settings_;
}

DocumentGridSettings& Document::grid_settings() noexcept {
  return grid_settings_;
}

const std::vector<DocumentGuide>& Document::guides() const noexcept {
  return guides_;
}

std::vector<DocumentGuide>& Document::guides() noexcept {
  return guides_;
}

const std::vector<Layer>& Document::layers() const noexcept {
  return layers_;
}

std::vector<Layer>& Document::layers() noexcept {
  return layers_;
}

std::optional<LayerId> Document::active_layer_id() const noexcept {
  return active_layer_id_;
}

Layer& Document::add_pixel_layer(std::string name, PixelBuffer pixels) {
  if (pixels.width() != width_ || pixels.height() != height_) {
    throw std::invalid_argument("Initial pixel layer must match document dimensions");
  }
  auto id = allocate_layer_id();
  layers_.emplace_back(id, std::move(name), std::move(pixels));
  active_layer_id_ = id;
  return layers_.back();
}

Layer& Document::add_layer(Layer layer) {
  if (layer.id() == 0) {
    throw std::invalid_argument("Layer id 0 is reserved");
  }
  next_layer_id_ = std::max(next_layer_id_, layer.id() + 1);
  layers_.push_back(std::move(layer));
  active_layer_id_ = layers_.back().id();
  return layers_.back();
}

Layer* Document::find_layer(LayerId id) noexcept {
  return find_layer_recursive(layers_, id);
}

const Layer* Document::find_layer(LayerId id) const noexcept {
  return find_layer_recursive(layers_, id);
}

void Document::set_active_layer(LayerId id) {
  if (find_layer(id) == nullptr) {
    throw std::invalid_argument("Cannot activate a layer that does not exist");
  }
  active_layer_id_ = id;
}

void Document::clear_active_layer() noexcept {
  active_layer_id_ = std::nullopt;
}

bool Document::remove_layer(LayerId id) {
  const auto removed = remove_layer_recursive(layers_, id);
  if (!removed) {
    return false;
  }

  if (active_layer_id_ == id || (active_layer_id_.has_value() && find_layer(*active_layer_id_) == nullptr)) {
    active_layer_id_ = last_layer_id(layers_);
  }
  return true;
}

void Document::resize_canvas(std::int32_t width, std::int32_t height) {
  if (width < 0 || height < 0) {
    throw std::invalid_argument("Document dimensions cannot be negative");
  }
  width_ = width;
  height_ = height;
}

LayerId Document::allocate_layer_id() noexcept {
  return next_layer_id_++;
}

Layer* Document::find_layer_recursive(std::vector<Layer>& layers, LayerId id) noexcept {
  for (auto& layer : layers) {
    if (layer.id() == id) {
      return &layer;
    }
    if (auto* found = find_layer_recursive(layer.children(), id)) {
      return found;
    }
  }
  return nullptr;
}

const Layer* Document::find_layer_recursive(const std::vector<Layer>& layers, LayerId id) const noexcept {
  for (const auto& layer : layers) {
    if (layer.id() == id) {
      return &layer;
    }
    if (auto* found = find_layer_recursive(layer.children(), id)) {
      return found;
    }
  }
  return nullptr;
}

bool Document::remove_layer_recursive(std::vector<Layer>& layers, LayerId id) noexcept {
  const auto found = std::find_if(layers.begin(), layers.end(), [id](const Layer& layer) { return layer.id() == id; });
  if (found != layers.end()) {
    layers.erase(found);
    return true;
  }

  for (auto& layer : layers) {
    if (remove_layer_recursive(layer.children(), id)) {
      return true;
    }
  }
  return false;
}

std::optional<LayerId> Document::last_layer_id(const std::vector<Layer>& layers) const noexcept {
  if (layers.empty()) {
    return std::nullopt;
  }

  const auto& layer = layers.back();
  if (!layer.children().empty()) {
    if (auto child_id = last_layer_id(layer.children())) {
      return child_id;
    }
  }
  return layer.id();
}

}  // namespace patchy
