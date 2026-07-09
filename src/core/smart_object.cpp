#include "core/smart_object.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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
