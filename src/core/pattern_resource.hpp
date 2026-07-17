#pragma once

#include "core/layer.hpp"
#include "core/pixel_buffer.hpp"

#include <array>
#include <string>
#include <string_view>
#include <vector>

// Pattern data model: the pixel tiles referenced by layer-style Pattern Overlay
// effects and Bevel & Emboss Texture sub-options. Tiles live document-globally
// (Photoshop stores them in the 'Patt'/'Pat2'/'Pat3' global tagged blocks) and
// effects reference them by id. Imported blocks stay raw-preserved in
// DocumentMetadata::unknown_psd_resources AND decode into this store for
// rendering; only Patchy-authored resources are serialized into a new pattern
// block on save (psd/psd_patterns.hpp).
namespace patchy {

enum class PatternProvenance {
  ImportedRaw,  // decoded from this document's preserved pattern block
  Authored      // added in Patchy (preset pick, cross-document paste); written on save
};

struct PatternResource {
  std::string id;    // GUID-shaped string joining the descriptor Ptrn 'Idnt'
  std::string name;  // display name (descriptor Ptrn 'Nm  ')
  PixelBuffer tile;  // decoded RGBA8 tile (implicitly shared, cheap to copy)
  PatternProvenance provenance{PatternProvenance::Authored};
};

struct PatternStore {
  std::vector<PatternResource> patterns;

  [[nodiscard]] bool empty() const noexcept;
  [[nodiscard]] const PatternResource* find(std::string_view id) const noexcept;
  // Inserts a copy unless the id is already present (first occurrence wins,
  // mirroring SmartObjectStore::adopt). Cross-document adoption must pass
  // provenance Authored so the target document's writer embeds the pixels.
  void adopt(const PatternResource& resource);
};

// True for tiles that can never produce paint: an empty buffer (poisoned
// adopt) or the PSD writer's 1x1 fully transparent placeholder (embedded so a
// pattern reference never dangles — Photoshop refuses to open files with
// unresolvable pattern references). adopt() lets a healthy tile replace these.
[[nodiscard]] bool pattern_tile_is_unrenderable(const PixelBuffer& tile) noexcept;

// Pattern ids referenced by a style (enabled or not — Photoshop keeps pattern
// data for disabled effects so they can be re-enabled), deduped.
void collect_referenced_pattern_ids(const LayerStyle& style, std::vector<std::string>& ids);

// Every pattern id this layer's saved form references: the style set above
// plus the vector shape content (a PtFl pattern fill and a vstk pattern
// stroke paint reference tiles by id exactly like pattern overlays). Does not
// recurse into children. A PSD must embed data for each of these ids —
// Photoshop refuses to open a file whose fill references a pattern it cannot
// resolve anywhere ("program error").
void collect_referenced_pattern_ids(const Layer& layer, std::vector<std::string>& ids);

// Appends the resources referenced by `layer` (and children) that resolve in
// `store` (mirror of collect_referenced_smart_object_sources).
void collect_referenced_pattern_resources(const Layer& layer, const PatternStore& store,
                                          std::vector<PatternResource>& resources);

// Fresh 8-4-4-4-12 lowercase-hex id, the shape Photoshop writes for 'Idnt'.
[[nodiscard]] std::string generate_pattern_uuid();

// The layer's effects reference point (the 16-byte 'fxrp' tagged block: two
// big-endian doubles). Photoshop updates it when a layer moves; linked
// (Algn=true) pattern overlays and bevel textures anchor their tile grid to it.
// Returns (0,0) when the block is absent or malformed. The block stays inside
// Layer::unknown_psd_blocks() so untouched layers re-emit it byte-identically;
// the setter patches those bytes in place (creating the block if needed).
[[nodiscard]] std::array<double, 2> layer_effects_reference_point(const Layer& layer) noexcept;
void set_layer_effects_reference_point(Layer& layer, double x, double y);

}  // namespace patchy
