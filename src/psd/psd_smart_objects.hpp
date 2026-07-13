#pragma once

#include "core/smart_filter.hpp"
#include "core/smart_object.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Binary parsing/serialization of the PSD smart-object structures: the per-layer
// 'SoLd'/'SoLE' (descriptor) and 'PlLd' (fixed-layout) placed-layer blocks, and the
// document-global 'lnkD'/'lnk2'/'lnk3' linked-file blocks holding the source payloads.
// Preservation contract: parsing never replaces the original bytes — sources keep their
// exact element spans for verbatim re-emit, and placed-layer blocks are regenerated
// PATCH-IN-PLACE (unmodeled descriptor fields survive) only once a layer's placement
// actually changed. Field layouts pinned against Photoshop 2026 output (July 2026 COM
// experiments; see docs/smart-objects.md).
namespace patchy::psd {

struct PlacedLayerInfo {
  SmartObjectPlacement placement;
  std::string placed_uuid;  // SoLd 'placed' (per-layer instance id; distinct from Idnt)
  std::string lock_reason;  // "" = editable; see kLayerMetadataSmartObjectLock
  // Native SoLd filterFX semantics. The original descriptor remains authoritative
  // in the preserved layer block; this model is inspection-only until checkpoint 7.
  std::optional<SmartFilterStack> smart_filters;
  // Present when the SoLd carries a real warp (style != warpNone or a mesh). A
  // SUPPORTED custom-envelope warp leaves lock_reason empty (Patchy re-renders it).
  std::optional<SmartObjectWarp> warp;
};

enum class SmartFilterDescriptorAction {
  Preserve,
  Replace,
  Remove,
};

struct SmartFilterDescriptorEdit {
  SmartFilterDescriptorAction action{SmartFilterDescriptorAction::Preserve};
  const SmartFilterStack* stack{nullptr};
  // When non-empty, one source accompanies each desired stack entry. An index
  // deep-clones that item from the original native filterFXList; nullopt
  // authors a canonical Gaussian item. Repeated indices duplicate an entry,
  // omitted indices delete entries, and a different order reorders them.
  // Leaving the vector empty retains the legacy patch-in-place rule
  // that the native and modeled lists must already have the same size.
  std::vector<std::optional<std::size_t>> entry_sources;

  SmartFilterDescriptorEdit() = default;
  SmartFilterDescriptorEdit(SmartFilterDescriptorAction wanted_action,
                            const SmartFilterStack* wanted_stack)
      : action(wanted_action), stack(wanted_stack) {}
};

// Parses a 'SoLd'/'SoLE' (descriptor) or 'PlLd'/'plLd' (fixed layout) payload.
// Returns nullopt when the payload cannot be understood (caller treats the layer as
// preview-locked with reason "unparsed").
[[nodiscard]] std::optional<PlacedLayerInfo> parse_placed_layer_block(std::string_view key,
                                                                      std::span<const std::uint8_t> payload);

// Parses a global linked-file block payload into sources (with verbatim element spans
// attached). Returns nullopt when any element fails to parse — the caller then keeps
// the whole block opaque.
[[nodiscard]] std::optional<std::vector<SmartObjectSource>> parse_linked_layer_block(
    std::span<const std::uint8_t> payload);

// Serializes a link block: the original payload verbatim while untouched; otherwise
// clean elements re-emit their original spans and dirty/new embedded sources are
// written as version-7 'liFD' elements.
[[nodiscard]] std::vector<std::uint8_t> serialize_linked_layer_block(const SmartObjectLinkBlock& block);

// Regenerates a placed-layer payload with the given placement patched in (uuid,
// transform quad, size, resolution; the warp bounds follow the quad when unwarped).
// Returns nullopt if the original cannot be parsed (caller keeps the original bytes).
[[nodiscard]] std::optional<std::vector<std::uint8_t>> regenerate_placed_layer_payload(
    std::string_view key, std::span<const std::uint8_t> original_payload, const SmartObjectPlacement& placement,
    const SmartObjectWarp* warp = nullptr, std::string_view placed_uuid = {},
    SmartFilterDescriptorEdit smart_filter_edit = {});

// Builds a from-scratch 'SoLd' payload for a freshly authored smart object (Convert /
// Place, M3), mirroring Photoshop 2026's exact field order and id forms (E1 captures,
// see docs/smart-objects.md). The caller stores it in the layer's unknown blocks so the normal
// preserve-unless-edited machinery emits it (and later edits patch it in place).
[[nodiscard]] std::vector<std::uint8_t> author_placed_layer_sold_payload(const SmartObjectPlacement& placement,
                                                                         std::string_view placed_uuid,
                                                                         const SmartFilterStack* smart_filters = nullptr);

}  // namespace patchy::psd
