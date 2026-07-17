#include "core/pattern_resource.hpp"

#include "core/vector_shape.hpp"

#include <algorithm>
#include <bit>
#include <cstdint>
#include <random>

namespace patchy {

namespace {

constexpr const char* kEffectsReferencePointKey = "fxrp";

void append_referenced_id(std::vector<std::string>& ids, const std::string& id) {
  if (id.empty()) {
    return;
  }
  if (std::find(ids.begin(), ids.end(), id) == ids.end()) {
    ids.push_back(id);
  }
}

double read_big_endian_double(const std::uint8_t* bytes) noexcept {
  std::uint64_t bits = 0;
  for (int i = 0; i < 8; ++i) {
    bits = (bits << 8U) | bytes[i];
  }
  return std::bit_cast<double>(bits);
}

void write_big_endian_double(std::uint8_t* bytes, double value) noexcept {
  auto bits = std::bit_cast<std::uint64_t>(value);
  for (int i = 7; i >= 0; --i) {
    bytes[i] = static_cast<std::uint8_t>(bits & 0xFFU);
    bits >>= 8U;
  }
}

}  // namespace

bool pattern_tile_is_unrenderable(const PixelBuffer& tile) noexcept {
  if (tile.empty()) {
    return true;
  }
  return tile.width() == 1 && tile.height() == 1 && tile.format() == PixelFormat::rgba8() &&
         tile.data()[3] == 0;
}

bool PatternStore::empty() const noexcept {
  return patterns.empty();
}

const PatternResource* PatternStore::find(std::string_view id) const noexcept {
  const auto it = std::find_if(patterns.begin(), patterns.end(),
                               [id](const PatternResource& resource) { return resource.id == id; });
  return it == patterns.end() ? nullptr : &*it;
}

void PatternStore::adopt(const PatternResource& resource) {
  if (resource.id.empty()) {
    return;
  }
  const auto it = std::find_if(patterns.begin(), patterns.end(),
                               [&resource](const PatternResource& existing) {
                                 return existing.id == resource.id;
                               });
  if (it != patterns.end()) {
    // A stored entry that can never render — an EMPTY tile (a poisoned adopt
    // from an earlier failure) or the writer's 1x1 fully transparent
    // dangling-reference placeholder — must not block a healthy same-id
    // resource from healing it. Healthy stored tiles stay untouched (the
    // document's pixels win).
    if (pattern_tile_is_unrenderable(it->tile) && !pattern_tile_is_unrenderable(resource.tile)) {
      *it = resource;
    }
    return;
  }
  patterns.push_back(resource);
}

void collect_referenced_pattern_ids(const LayerStyle& style, std::vector<std::string>& ids) {
  for (const auto& overlay : style.pattern_overlays) {
    append_referenced_id(ids, overlay.pattern_id);
  }
  for (const auto& bevel : style.bevels) {
    append_referenced_id(ids, bevel.texture.pattern_id);
  }
}

void collect_referenced_pattern_ids(const Layer& layer, std::vector<std::string>& ids) {
  collect_referenced_pattern_ids(layer.layer_style(), ids);
  if (const auto* content = layer.vector_shape(); content != nullptr) {
    // Only the active kind reaches the file (the fill block is SoCo/GdFl/PtFl
    // by kind, matching Photoshop, which likewise drops switched-away
    // pattern data).
    if (content->fill.kind == VectorFillKind::Pattern) {
      append_referenced_id(ids, content->fill.pattern_id);
    }
    if (content->stroke.content.kind == VectorFillKind::Pattern) {
      append_referenced_id(ids, content->stroke.content.pattern_id);
    }
  }
}

void collect_referenced_pattern_resources(const Layer& layer, const PatternStore& store,
                                          std::vector<PatternResource>& resources) {
  std::vector<std::string> ids;
  collect_referenced_pattern_ids(layer, ids);
  for (const auto& id : ids) {
    if (const auto* resource = store.find(id); resource != nullptr) {
      const auto already_collected =
          std::any_of(resources.begin(), resources.end(),
                      [&id](const PatternResource& collected) { return collected.id == id; });
      if (!already_collected) {
        resources.push_back(*resource);
      }
    }
  }
  for (const auto& child : layer.children()) {
    collect_referenced_pattern_resources(child, store, resources);
  }
}

std::string generate_pattern_uuid() {
  static std::mt19937_64 engine{std::random_device{}()};
  const auto hex_digits = "0123456789abcdef";
  // Randomness here never feeds pixel output, so the cross-toolchain render
  // determinism rule does not apply (same as generate_smart_object_uuid).
  std::string uuid;
  uuid.reserve(36);
  for (const int group : {8, 4, 4, 4, 12}) {
    if (!uuid.empty()) {
      uuid.push_back('-');
    }
    for (int i = 0; i < group; ++i) {
      uuid.push_back(hex_digits[engine() & 0xFU]);
    }
  }
  return uuid;
}

std::array<double, 2> layer_effects_reference_point(const Layer& layer) noexcept {
  for (const auto& block : layer.unknown_psd_blocks()) {
    if (block.key == kEffectsReferencePointKey && block.payload.size() == 16U) {
      return {read_big_endian_double(block.payload.data()),
              read_big_endian_double(block.payload.data() + 8)};
    }
  }
  return {0.0, 0.0};
}

void set_layer_effects_reference_point(Layer& layer, double x, double y) {
  auto& blocks = layer.unknown_psd_blocks();
  for (auto& block : blocks) {
    if (block.key == kEffectsReferencePointKey) {
      block.payload.resize(16U);
      write_big_endian_double(block.payload.data(), x);
      write_big_endian_double(block.payload.data() + 8, y);
      return;
    }
  }
  UnknownPsdBlock block;
  block.key = kEffectsReferencePointKey;
  block.payload.resize(16U);
  write_big_endian_double(block.payload.data(), x);
  write_big_endian_double(block.payload.data() + 8, y);
  blocks.push_back(std::move(block));
}

}  // namespace patchy
