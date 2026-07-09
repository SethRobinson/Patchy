#include "core/smart_object.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <utility>

namespace patchy {

namespace {

std::optional<std::string_view> metadata_value(const Layer& layer, const char* key) {
  const auto& metadata = std::as_const(layer).metadata();
  const auto found = metadata.find(key);
  if (found == metadata.end()) {
    return std::nullopt;
  }
  return std::string_view(found->second);
}

std::optional<double> parse_double_token(const char*& cursor) {
  char* end = nullptr;
  const auto value = std::strtod(cursor, &end);
  if (end == cursor) {
    return std::nullopt;
  }
  cursor = end;
  return value;
}

std::string format_double(double value) {
  std::array<char, 32> buffer{};
  // %.17g round-trips every finite double exactly.
  std::snprintf(buffer.data(), buffer.size(), "%.17g", value);
  return std::string(buffer.data());
}

}  // namespace

bool SmartObjectStore::empty() const noexcept {
  return blocks.empty();
}

const SmartObjectSource* SmartObjectStore::find(std::string_view uuid) const noexcept {
  for (const auto& block : blocks) {
    for (const auto& source : block.sources) {
      if (source.uuid == uuid) {
        return &source;
      }
    }
  }
  return nullptr;
}

SmartObjectSource* SmartObjectStore::find(std::string_view uuid) noexcept {
  return const_cast<SmartObjectSource*>(std::as_const(*this).find(uuid));
}

SmartObjectSource& SmartObjectStore::add_embedded(std::string uuid, std::string filename, std::string filetype,
                                                  std::shared_ptr<const std::vector<std::uint8_t>> bytes) {
  SmartObjectLinkBlock* target = nullptr;
  for (auto& block : blocks) {
    if (block.key == "lnk2" && !block.opaque) {
      target = &block;
      break;
    }
  }
  if (target == nullptr) {
    blocks.push_back(SmartObjectLinkBlock{});
    target = &blocks.back();
  }
  target->original_payload.reset();  // the block's contents change; regenerate on save
  SmartObjectSource source;
  source.kind = SmartObjectSourceKind::Embedded;
  source.uuid = std::move(uuid);
  source.filename = std::move(filename);
  source.filetype = std::move(filetype);
  source.file_bytes = std::move(bytes);
  source.dirty = true;
  target->sources.push_back(std::move(source));
  return target->sources.back();
}

bool SmartObjectStore::remove(std::string_view uuid) {
  for (auto& block : blocks) {
    const auto found = std::find_if(block.sources.begin(), block.sources.end(),
                                    [uuid](const SmartObjectSource& source) { return source.uuid == uuid; });
    if (found != block.sources.end()) {
      block.sources.erase(found);
      block.original_payload.reset();  // the block's element list changed; regenerate on save
      return true;
    }
  }
  return false;
}

void SmartObjectStore::adopt(const SmartObjectSource& source) {
  if (find(source.uuid) != nullptr) {
    return;
  }
  SmartObjectLinkBlock* target = nullptr;
  for (auto& block : blocks) {
    if (block.key == "lnk2" && !block.opaque) {
      target = &block;
      break;
    }
  }
  if (target == nullptr) {
    blocks.push_back(SmartObjectLinkBlock{});
    target = &blocks.back();
  }
  target->original_payload.reset();  // the block's element list changes
  target->sources.push_back(source);
}

void collect_referenced_smart_object_sources(const Layer& layer, const SmartObjectStore& store,
                                             std::vector<SmartObjectSource>& sources) {
  if (layer_is_smart_object(layer)) {
    const auto uuid = smart_object_source_uuid(layer);
    if (const auto* source = store.find(uuid); source != nullptr) {
      const auto already_collected =
          std::any_of(sources.begin(), sources.end(),
                      [&uuid](const SmartObjectSource& collected) { return collected.uuid == uuid; });
      if (!already_collected) {
        sources.push_back(*source);
      }
    }
  }
  for (const auto& child : layer.children()) {
    collect_referenced_smart_object_sources(child, store, sources);
  }
}

bool layer_is_smart_object(const Layer& layer) {
  return metadata_value(layer, kLayerMetadataSmartObject).has_value();
}

std::string smart_object_source_uuid(const Layer& layer) {
  return std::string(metadata_value(layer, kLayerMetadataSmartObject).value_or(std::string_view{}));
}

std::string smart_object_lock_reason(const Layer& layer) {
  return std::string(metadata_value(layer, kLayerMetadataSmartObjectLock).value_or(std::string_view{}));
}

bool layer_smart_object_block_dirty(const Layer& layer) {
  return metadata_value(layer, kLayerMetadataSmartObjectBlockDirty).has_value();
}

void mark_layer_smart_object_block_dirty(Layer& layer) {
  if (layer_is_smart_object(layer) && !layer_smart_object_block_dirty(layer)) {
    layer.metadata()[kLayerMetadataSmartObjectBlockDirty] = "1";
  }
}

std::string serialize_smart_object_transform(const std::array<double, 8>& transform) {
  std::string serialized;
  for (std::size_t i = 0; i < transform.size(); ++i) {
    if (i != 0) {
      serialized.push_back(' ');
    }
    serialized += format_double(transform[i]);
  }
  return serialized;
}

std::optional<std::array<double, 8>> parse_smart_object_transform(std::string_view text) {
  const std::string copy(text);
  const char* cursor = copy.c_str();
  std::array<double, 8> transform{};
  for (auto& value : transform) {
    const auto parsed = parse_double_token(cursor);
    if (!parsed.has_value()) {
      return std::nullopt;
    }
    value = *parsed;
  }
  return transform;
}

std::string generate_smart_object_uuid() {
  static std::mt19937_64 engine{std::random_device{}()};
  const auto hex_digits = "0123456789abcdef";
  // 8-4-4-4-12 like Photoshop's Idnt values. Randomness here never feeds pixel
  // output, so the cross-toolchain determinism rule for render code does not apply.
  std::string uuid;
  uuid.reserve(36);
  for (const int group : {8, 4, 4, 4, 12}) {
    if (!uuid.empty()) {
      uuid.push_back('-');
    }
    for (int i = 0; i < group; ++i) {
      uuid.push_back(hex_digits[engine() & 0xF]);
    }
  }
  return uuid;
}

SmartObjectPlacement rescaled_smart_object_placement(const SmartObjectPlacement& placement, double new_width,
                                                     double new_height, double new_dpi) {
  SmartObjectPlacement result = placement;
  const auto& quad = placement.transform;
  const double old_width = placement.width > 0.0 ? placement.width : 1.0;
  const double old_height = placement.height > 0.0 ? placement.height : 1.0;
  const double old_dpi = placement.resolution > 0.0 ? placement.resolution : 72.0;
  const double dpi = new_dpi > 0.0 ? new_dpi : 72.0;
  const double center_x = (quad[0] + quad[2] + quad[4] + quad[6]) / 4.0;
  const double center_y = (quad[1] + quad[3] + quad[5] + quad[7]) / 4.0;
  // Document-space step of one content pixel along the content x/y axes (corner
  // order is top-left, top-right, bottom-right, bottom-left).
  const double axis_x_x = (quad[2] - quad[0]) / old_width;
  const double axis_x_y = (quad[3] - quad[1]) / old_width;
  const double axis_y_x = (quad[6] - quad[0]) / old_height;
  const double axis_y_y = (quad[7] - quad[1]) / old_height;
  // Preserving the content-inch map means one NEW content pixel steps by dpi_old/dpi_new
  // of an old pixel's step.
  const double density_scale = old_dpi / dpi;
  const double half_width = new_width / 2.0 * density_scale;
  const double half_height = new_height / 2.0 * density_scale;
  result.transform[0] = center_x - axis_x_x * half_width - axis_y_x * half_height;
  result.transform[1] = center_y - axis_x_y * half_width - axis_y_y * half_height;
  result.transform[2] = center_x + axis_x_x * half_width - axis_y_x * half_height;
  result.transform[3] = center_y + axis_x_y * half_width - axis_y_y * half_height;
  result.transform[4] = center_x + axis_x_x * half_width + axis_y_x * half_height;
  result.transform[5] = center_y + axis_x_y * half_width + axis_y_y * half_height;
  result.transform[6] = center_x - axis_x_x * half_width + axis_y_x * half_height;
  result.transform[7] = center_y - axis_x_y * half_width + axis_y_y * half_height;
  result.width = new_width;
  result.height = new_height;
  result.resolution = dpi;
  return result;
}

std::optional<SmartObjectPlacement> smart_object_placement_from_layer(const Layer& layer) {
  const auto uuid = metadata_value(layer, kLayerMetadataSmartObject);
  const auto transform_text = metadata_value(layer, kLayerMetadataSmartObjectTransform);
  if (!uuid.has_value() || !transform_text.has_value()) {
    return std::nullopt;
  }
  const auto transform = parse_smart_object_transform(*transform_text);
  if (!transform.has_value()) {
    return std::nullopt;
  }
  SmartObjectPlacement placement;
  placement.uuid = std::string(*uuid);
  placement.transform = *transform;
  if (const auto size_text = metadata_value(layer, kLayerMetadataSmartObjectSize); size_text.has_value()) {
    const std::string copy(*size_text);
    const char* cursor = copy.c_str();
    const auto width = parse_double_token(cursor);
    const auto height = parse_double_token(cursor);
    placement.width = width.value_or(0.0);
    placement.height = height.value_or(0.0);
  }
  if (const auto resolution = metadata_value(layer, kLayerMetadataSmartObjectResolution); resolution.has_value()) {
    const std::string copy(*resolution);
    const char* cursor = copy.c_str();
    placement.resolution = parse_double_token(cursor).value_or(72.0);
  }
  if (const auto type = metadata_value(layer, kLayerMetadataSmartObjectType); type.has_value()) {
    placement.placed_type = std::atoi(std::string(*type).c_str());
  }
  if (const auto anti_alias = metadata_value(layer, kLayerMetadataSmartObjectAntiAlias); anti_alias.has_value()) {
    placement.anti_alias = std::atoi(std::string(*anti_alias).c_str());
  }
  return placement;
}

void store_smart_object_placement(Layer& layer, const SmartObjectPlacement& placement) {
  auto& metadata = layer.metadata();
  metadata[kLayerMetadataSmartObject] = placement.uuid;
  metadata[kLayerMetadataSmartObjectTransform] = serialize_smart_object_transform(placement.transform);
  metadata[kLayerMetadataSmartObjectSize] = format_double(placement.width) + " " + format_double(placement.height);
  metadata[kLayerMetadataSmartObjectResolution] = format_double(placement.resolution);
  metadata[kLayerMetadataSmartObjectType] = std::to_string(placement.placed_type);
  metadata[kLayerMetadataSmartObjectAntiAlias] = std::to_string(placement.anti_alias);
}

void set_layer_smart_object_metadata(Layer& layer, const SmartObjectPlacement& placement,
                                     std::string_view placed_uuid, std::string_view source_block,
                                     std::string_view lock_reason, std::string_view raster_status) {
  store_smart_object_placement(layer, placement);
  auto& metadata = layer.metadata();
  if (!placed_uuid.empty()) {
    metadata[kLayerMetadataSmartObjectPlaced] = std::string(placed_uuid);
  }
  metadata[kLayerMetadataSmartObjectSourceBlock] = std::string(source_block);
  if (lock_reason.empty()) {
    metadata.erase(kLayerMetadataSmartObjectLock);
  } else {
    metadata[kLayerMetadataSmartObjectLock] = std::string(lock_reason);
  }
  metadata[kLayerMetadataSmartObjectRasterStatus] = std::string(raster_status);
}

void clear_layer_smart_object_metadata(Layer& layer) {
  if (!layer_is_smart_object(layer)) {
    return;
  }
  auto& metadata = layer.metadata();
  for (auto it = metadata.begin(); it != metadata.end();) {
    if (it->first.rfind(kLayerMetadataSmartObject, 0) == 0) {
      it = metadata.erase(it);
    } else {
      ++it;
    }
  }
}

void strip_layer_smart_object_data(Layer& layer) {
  clear_layer_smart_object_metadata(layer);
  const auto has_placed_block =
      std::any_of(std::as_const(layer).unknown_psd_blocks().begin(),
                  std::as_const(layer).unknown_psd_blocks().end(), [](const UnknownPsdBlock& block) {
                    return block.key == "SoLd" || block.key == "SoLE" || block.key == "PlLd" || block.key == "plLd";
                  });
  if (!has_placed_block) {
    return;
  }
  auto& blocks = layer.unknown_psd_blocks();
  blocks.erase(std::remove_if(blocks.begin(), blocks.end(),
                              [](const UnknownPsdBlock& block) {
                                return block.key == "SoLd" || block.key == "SoLE" || block.key == "PlLd" ||
                                       block.key == "plLd";
                              }),
               blocks.end());
}

}  // namespace patchy
