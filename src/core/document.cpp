#include "core/document.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

namespace patchy {

namespace {

void collect_smart_filter_placed_uuids(const Layer& layer,
                                       std::vector<std::string>& placed_uuids) {
  const auto placed_uuid = smart_object_placed_uuid(layer);
  if (!placed_uuid.empty() &&
      std::find(placed_uuids.begin(), placed_uuids.end(), placed_uuid) == placed_uuids.end()) {
    placed_uuids.push_back(placed_uuid);
  }
  for (const auto& child : layer.children()) {
    collect_smart_filter_placed_uuids(child, placed_uuids);
  }
}

bool layer_tree_references_placed_uuid(const std::vector<Layer>& layers,
                                       std::string_view placed_uuid) {
  return std::any_of(layers.begin(), layers.end(), [placed_uuid](const Layer& layer) {
    return smart_object_placed_uuid(layer) == placed_uuid ||
           layer_tree_references_placed_uuid(layer.children(), placed_uuid);
  });
}

}  // namespace

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

void Document::set_format(PixelFormat format) noexcept {
  format_ = format;
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

const std::optional<DocumentPaletteEditing>& Document::palette_editing() const noexcept {
  return palette_editing_;
}

std::optional<DocumentPaletteEditing>& Document::palette_editing() noexcept {
  return palette_editing_;
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

const std::vector<DocumentChannel>& Document::channels() const noexcept {
  return channels_;
}

std::vector<DocumentChannel>& Document::channels() noexcept {
  return channels_;
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

DocumentChannel& Document::add_channel(DocumentChannel channel) {
  if (channel.id() == 0) {
    throw std::invalid_argument("Document channel id 0 is reserved");
  }
  if (find_channel(channel.id()) != nullptr) {
    throw std::invalid_argument("Document channel ids must be unique");
  }
  const auto& pixels = std::as_const(channel).pixels();
  if (pixels.format() != PixelFormat::gray8()) {
    throw std::invalid_argument("Document channels must use 8-bit grayscale pixels");
  }
  if (pixels.width() != width_ || pixels.height() != height_) {
    throw std::invalid_argument("Document channels must match document dimensions");
  }
  if (channels_.size() >= maximum_saved_channel_count()) {
    throw std::length_error("Document has reached Photoshop's 56-channel limit");
  }

  const auto id = channel.id();
  channels_.push_back(std::move(channel));
  if (id >= next_channel_id_) {
    next_channel_id_ = id == std::numeric_limits<ChannelId>::max() ? 1 : id + 1;
  }
  return channels_.back();
}

DocumentChannel* Document::find_channel(ChannelId id) noexcept {
  const auto found = std::find_if(channels_.begin(), channels_.end(),
                                  [id](const DocumentChannel& channel) { return channel.id() == id; });
  return found == channels_.end() ? nullptr : &*found;
}

const DocumentChannel* Document::find_channel(ChannelId id) const noexcept {
  const auto found = std::find_if(channels_.begin(), channels_.end(),
                                  [id](const DocumentChannel& channel) { return channel.id() == id; });
  return found == channels_.end() ? nullptr : &*found;
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
  std::vector<std::string> removed_placed_uuids;
  if (const auto* layer = std::as_const(*this).find_layer(id); layer != nullptr) {
    collect_smart_filter_placed_uuids(*layer, removed_placed_uuids);
  }
  const auto removed = remove_layer_recursive(layers_, id);
  if (!removed) {
    return false;
  }

  if (active_layer_id_ == id || (active_layer_id_.has_value() && find_layer(*active_layer_id_) == nullptr)) {
    active_layer_id_ = last_layer_id(layers_);
  }
  for (const auto& placed_uuid : removed_placed_uuids) {
    if (!layer_tree_references_placed_uuid(std::as_const(*this).layers(), placed_uuid)) {
      (void)metadata_.smart_filter_effects.remove(placed_uuid);
    }
  }
  return true;
}

bool Document::remove_channel(ChannelId id) {
  const auto found = std::find_if(channels_.begin(), channels_.end(),
                                  [id](const DocumentChannel& channel) { return channel.id() == id; });
  if (found == channels_.end()) {
    return false;
  }
  channels_.erase(found);
  return true;
}

bool Document::rename_channel(ChannelId id, std::string name) {
  auto* channel = find_channel(id);
  if (channel == nullptr) {
    return false;
  }
  channel->set_name(std::move(name));
  return true;
}

bool Document::reorder_channel(ChannelId id, std::size_t final_index) {
  if (final_index >= channels_.size()) {
    throw std::out_of_range("Document channel reorder index is out of range");
  }
  const auto found = std::find_if(channels_.begin(), channels_.end(),
                                  [id](const DocumentChannel& channel) { return channel.id() == id; });
  if (found == channels_.end()) {
    return false;
  }
  if (static_cast<std::size_t>(std::distance(channels_.begin(), found)) == final_index) {
    return true;
  }

  auto moved = std::move(*found);
  channels_.erase(found);
  channels_.insert(channels_.begin() + static_cast<std::ptrdiff_t>(final_index), std::move(moved));
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

ChannelId Document::allocate_channel_id() {
  if (next_channel_id_ == 0) {
    next_channel_id_ = 1;
  }
  const auto first_candidate = next_channel_id_;
  do {
    const auto candidate = next_channel_id_;
    next_channel_id_ = candidate == std::numeric_limits<ChannelId>::max() ? 1 : candidate + 1;
    if (find_channel(candidate) == nullptr) {
      return candidate;
    }
  } while (next_channel_id_ != first_candidate);
  throw std::overflow_error("No document channel ids remain available");
}

std::string Document::next_alpha_channel_name() const {
  for (std::uint64_t number = 1;; ++number) {
    const auto candidate = std::string("Alpha ") + std::to_string(number);
    const auto exists = std::any_of(channels_.begin(), channels_.end(), [&](const DocumentChannel& channel) {
      return channel.name() == candidate;
    });
    if (!exists) {
      return candidate;
    }
    if (number == std::numeric_limits<std::uint64_t>::max()) {
      throw std::overflow_error("No alpha channel names remain available");
    }
  }
}

std::size_t Document::maximum_saved_channel_count(bool includes_merged_transparency) const noexcept {
  std::size_t component_count = 3U;
  switch (format_.color_mode) {
    case ColorMode::Grayscale:
      component_count = 1U;
      break;
    case ColorMode::RGB:
    case ColorMode::Lab:
      component_count = 3U;
      break;
    case ColorMode::CMYK:
      component_count = 4U;
      break;
  }
  return kMaximumPhotoshopChannelCount - component_count - (includes_merged_transparency ? 1U : 0U);
}

Layer* Document::find_layer_recursive(std::vector<Layer>& layers, LayerId id) noexcept {
  // A lookup must not count as a mutation: descending through the non-const
  // children() accessor bumped every visited layer's revisions on every
  // find_layer call (thousands of spurious bumps per frame), defeating the
  // revision-keyed thumbnail and style-mask caches. Walk const, cast the hit.
  for (auto& layer : layers) {
    if (layer.id() == id) {
      return &layer;
    }
    if (const auto* found = find_layer_recursive(std::as_const(layer).children(), id)) {
      return const_cast<Layer*>(found);
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
