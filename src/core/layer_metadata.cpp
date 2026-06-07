#include "core/layer_metadata.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <sstream>
#include <utility>

namespace patchy {

namespace {

bool has_all_flags(LayerLockFlags value, LayerLockFlags flags) {
  return flags != kLayerLockNone && (value & flags) == flags;
}

bool has_any_flags(LayerLockFlags value, LayerLockFlags flags) {
  return (value & flags) != kLayerLockNone;
}

std::int16_t read_i16_be(const std::vector<std::uint8_t>& bytes, std::size_t offset) noexcept {
  return static_cast<std::int16_t>((static_cast<std::uint16_t>(bytes[offset]) << 8U) |
                                  static_cast<std::uint16_t>(bytes[offset + 1U]));
}

std::int32_t read_i32_be(const std::vector<std::uint8_t>& bytes, std::size_t offset) noexcept {
  return static_cast<std::int32_t>((static_cast<std::uint32_t>(bytes[offset]) << 24U) |
                                  (static_cast<std::uint32_t>(bytes[offset + 1U]) << 16U) |
                                  (static_cast<std::uint32_t>(bytes[offset + 2U]) << 8U) |
                                  static_cast<std::uint32_t>(bytes[offset + 3U]));
}

void write_i32_be(std::vector<std::uint8_t>& bytes, std::size_t offset, std::int32_t value) noexcept {
  const auto raw = static_cast<std::uint32_t>(value);
  bytes[offset] = static_cast<std::uint8_t>((raw >> 24U) & 0xffU);
  bytes[offset + 1U] = static_cast<std::uint8_t>((raw >> 16U) & 0xffU);
  bytes[offset + 2U] = static_cast<std::uint8_t>((raw >> 8U) & 0xffU);
  bytes[offset + 3U] = static_cast<std::uint8_t>(raw & 0xffU);
}

std::int32_t fixed_path_delta(std::int32_t delta, std::int32_t extent) noexcept {
  if (delta == 0 || extent <= 0) {
    return 0;
  }
  constexpr double kPathFixedScale = 16777216.0;
  const auto scaled = std::llround((static_cast<double>(delta) * kPathFixedScale) / static_cast<double>(extent));
  return static_cast<std::int32_t>(
      std::clamp(scaled, static_cast<long long>(std::numeric_limits<std::int32_t>::min()),
                 static_cast<long long>(std::numeric_limits<std::int32_t>::max())));
}

std::int32_t add_i32_saturated(std::int32_t value, std::int32_t delta) noexcept {
  const auto sum = static_cast<long long>(value) + static_cast<long long>(delta);
  return static_cast<std::int32_t>(
      std::clamp(sum, static_cast<long long>(std::numeric_limits<std::int32_t>::min()),
                 static_cast<long long>(std::numeric_limits<std::int32_t>::max())));
}

bool is_photoshop_vector_mask_block(std::string_view key) noexcept {
  return key == "vmsk" || key == "vsms";
}

void translate_vector_mask_coordinate(std::vector<std::uint8_t>& payload, std::size_t offset,
                                      std::int32_t delta) noexcept {
  if (delta == 0) {
    return;
  }
  write_i32_be(payload, offset, add_i32_saturated(read_i32_be(payload, offset), delta));
}

void translate_vector_mask_payload(std::vector<std::uint8_t>& payload, std::int32_t dx, std::int32_t dy,
                                   std::int32_t document_width, std::int32_t document_height) noexcept {
  if (payload.size() < 8U) {
    return;
  }

  const auto fixed_dx = fixed_path_delta(dx, document_width);
  const auto fixed_dy = fixed_path_delta(dy, document_height);
  if (fixed_dx == 0 && fixed_dy == 0) {
    return;
  }

  constexpr std::size_t kPathRecordSize = 26U;
  for (std::size_t record = 8U; record + kPathRecordSize <= payload.size(); record += kPathRecordSize) {
    const auto selector = read_i16_be(payload, record);
    if (selector != 1 && selector != 2 && selector != 4 && selector != 5) {
      continue;
    }

    translate_vector_mask_coordinate(payload, record + 2U, fixed_dy);
    translate_vector_mask_coordinate(payload, record + 6U, fixed_dx);
    translate_vector_mask_coordinate(payload, record + 10U, fixed_dy);
    translate_vector_mask_coordinate(payload, record + 14U, fixed_dx);
    translate_vector_mask_coordinate(payload, record + 18U, fixed_dy);
    translate_vector_mask_coordinate(payload, record + 22U, fixed_dx);
  }
}

}  // namespace

bool layer_locks_transparent_pixels(const Layer& layer) {
  return has_all_flags(layer.lock_flags(), kLayerLockTransparentPixels);
}

void set_layer_locks_transparent_pixels(Layer& layer, bool locked) {
  set_layer_lock_flag(layer, kLayerLockTransparentPixels, locked);
}

bool layer_locks_image_pixels(const Layer& layer) {
  return has_all_flags(layer.lock_flags(), kLayerLockImagePixels);
}

void set_layer_locks_image_pixels(Layer& layer, bool locked) {
  set_layer_lock_flag(layer, kLayerLockImagePixels, locked);
}

bool layer_locks_position(const Layer& layer) {
  return has_all_flags(layer.lock_flags(), kLayerLockPosition);
}

void set_layer_locks_position(Layer& layer, bool locked) {
  set_layer_lock_flag(layer, kLayerLockPosition, locked);
}

bool layer_locks_all(const Layer& layer) {
  return (layer.lock_flags() & kLayerLockAll) == kLayerLockAll;
}

void set_layer_locks_all(Layer& layer, bool locked) {
  set_layer_lock_flags(layer, locked ? kLayerLockAll : kLayerLockNone);
}

LayerLockFlags layer_lock_flags(const Layer& layer) {
  return layer.lock_flags();
}

void set_layer_lock_flags(Layer& layer, LayerLockFlags flags) {
  layer.set_lock_flags(flags);
}

void set_layer_lock_flag(Layer& layer, LayerLockFlags flag, bool locked) {
  auto flags = layer.lock_flags();
  if (locked) {
    flags |= flag;
  } else {
    flags &= ~flag;
  }
  set_layer_lock_flags(layer, flags);
}

bool layer_is_locked(const Layer& layer) {
  return layer_locks_all(layer);
}

void set_layer_locked(Layer& layer, bool locked) {
  set_layer_locks_all(layer, locked);
}

namespace {

struct LayerLockSearchResult {
  bool found{false};
  LayerLockFlags flags{kLayerLockNone};
  LayerLockFlags ancestor_flags{kLayerLockNone};
};

LayerLockSearchResult find_effective_lock(const std::vector<Layer>& layers, LayerId layer_id,
                                          LayerLockFlags ancestor_flags) {
  for (const auto& layer : layers) {
    const auto effective_flags = ancestor_flags | layer.lock_flags();
    if (layer.id() == layer_id) {
      return LayerLockSearchResult{true, effective_flags, ancestor_flags};
    }
    if (layer.kind() == LayerKind::Group) {
      if (auto found = find_effective_lock(layer.children(), layer_id, effective_flags); found.found) {
        return found;
      }
    }
  }
  return {};
}

}  // namespace

LayerLockFlags layer_effective_lock_flags(const std::vector<Layer>& layers, LayerId layer_id) {
  return find_effective_lock(layers, layer_id, kLayerLockNone).flags & kLayerLockAll;
}

LayerLockFlags layer_ancestor_lock_flags(const std::vector<Layer>& layers, LayerId layer_id) {
  return find_effective_lock(layers, layer_id, kLayerLockNone).ancestor_flags & kLayerLockAll;
}

bool layer_effectively_locks_transparent_pixels(const std::vector<Layer>& layers, LayerId layer_id) {
  return has_any_flags(layer_effective_lock_flags(layers, layer_id), kLayerLockTransparentPixels);
}

bool layer_effectively_locks_image_pixels(const std::vector<Layer>& layers, LayerId layer_id) {
  return has_any_flags(layer_effective_lock_flags(layers, layer_id), kLayerLockImagePixels);
}

bool layer_effectively_locks_position(const std::vector<Layer>& layers, LayerId layer_id) {
  return has_any_flags(layer_effective_lock_flags(layers, layer_id), kLayerLockPosition);
}

bool layer_is_effectively_locked(const std::vector<Layer>& layers, LayerId layer_id) {
  return (layer_effective_lock_flags(layers, layer_id) & kLayerLockAll) == kLayerLockAll;
}

bool layer_has_locked_ancestor(const std::vector<Layer>& layers, LayerId layer_id) {
  return layer_ancestor_lock_flags(layers, layer_id) != kLayerLockNone;
}

bool layer_mask_linked(const Layer& layer) {
  const auto found = layer.metadata().find(kLayerMetadataMaskLinked);
  return found == layer.metadata().end() || found->second != "false";
}

void set_layer_mask_linked(Layer& layer, bool linked) {
  if (linked) {
    layer.metadata().erase(kLayerMetadataMaskLinked);
  } else {
    layer.metadata()[kLayerMetadataMaskLinked] = "false";
  }
}

bool layer_group_expanded(const Layer& layer) {
  const auto found = layer.metadata().find(kLayerMetadataGroupExpanded);
  return found == layer.metadata().end() || found->second != "false";
}

void set_layer_group_expanded(Layer& layer, bool expanded) {
  if (expanded) {
    layer.metadata()[kLayerMetadataGroupExpanded] = "true";
  } else {
    layer.metadata()[kLayerMetadataGroupExpanded] = "false";
  }
}

bool layer_is_text(const Layer& layer) {
  return layer.metadata().contains(kLayerMetadataText);
}

std::optional<LayerAffineTransform> parse_layer_affine_transform(std::string_view text) {
  std::istringstream stream{std::string(text)};
  LayerAffineTransform transform{};
  for (auto& value : transform) {
    if (!(stream >> value) || !std::isfinite(value)) {
      return std::nullopt;
    }
  }
  return transform;
}

std::string serialize_layer_affine_transform(const LayerAffineTransform& transform) {
  std::ostringstream stream;
  stream << std::setprecision(12);
  for (std::size_t i = 0; i < transform.size(); ++i) {
    if (i != 0U) {
      stream << ' ';
    }
    stream << transform[i];
  }
  return stream.str();
}

LayerAffineTransform compose_layer_affine_transform(const LayerAffineTransform& outer,
                                                    const LayerAffineTransform& inner) {
  return LayerAffineTransform{
      outer[0] * inner[0] + outer[2] * inner[1],
      outer[1] * inner[0] + outer[3] * inner[1],
      outer[0] * inner[2] + outer[2] * inner[3],
      outer[1] * inner[2] + outer[3] * inner[3],
      outer[0] * inner[4] + outer[2] * inner[5] + outer[4],
      outer[1] * inner[4] + outer[3] * inner[5] + outer[5],
  };
}

void translate_moved_layer_metadata(Layer& layer, std::int32_t dx, std::int32_t dy, std::int32_t document_width,
                                    std::int32_t document_height) {
  if (dx == 0 && dy == 0) {
    return;
  }

  if (layer_mask_linked(layer)) {
    if (std::as_const(layer).mask().has_value()) {
      auto& mask = layer.mask();
      mask->bounds.x += dx;
      mask->bounds.y += dy;
    }

    const auto has_vector_mask =
        std::any_of(std::as_const(layer).unknown_psd_blocks().begin(), std::as_const(layer).unknown_psd_blocks().end(),
                    [](const UnknownPsdBlock& block) { return is_photoshop_vector_mask_block(block.key); });
    if (has_vector_mask) {
      for (auto& block : layer.unknown_psd_blocks()) {
        if (is_photoshop_vector_mask_block(block.key)) {
          translate_vector_mask_payload(block.payload, dx, dy, document_width, document_height);
        }
      }
    }
  }

  if (!layer_is_text(layer)) {
    return;
  }
  auto& metadata = layer.metadata();
  auto found = metadata.find(kLayerMetadataTextTransform);
  if (found == metadata.end()) {
    return;
  }
  auto transform = parse_layer_affine_transform(found->second);
  if (!transform.has_value()) {
    return;
  }
  (*transform)[4] += static_cast<double>(dx);
  (*transform)[5] += static_cast<double>(dy);
  found->second = serialize_layer_affine_transform(*transform);
}

}  // namespace patchy
