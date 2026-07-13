#pragma once

#include "core/layer.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace patchy {

// Raw document-global Photoshop Smart Filter cache data ('FEid'/'FXid'). The
// cache records can be very large, so records point into shared block storage.
// Whole-Document undo snapshots consequently copy only shared_ptrs, not cache
// payloads. Decoded filter-mask samples use the same sharing rule.
struct SmartFilterEffectsMask {
  Rect bounds{};
  std::shared_ptr<const std::vector<std::uint8_t>> samples;
};

struct SmartFilterEffectsRecord {
  // The document-global block this record came from. These fields let a record
  // carried through the clipboard be adopted without normalizing FEid/FXid or
  // its outer version/length width.
  std::string source_block_key{"FEid"};
  std::uint32_t source_block_version{3};
  bool source_long_length{false};

  // Per-layer SoLd 'placed' identifier. original_placed_uuid names the bytes in
  // raw_storage; placed_uuid may differ after clone/rekey.
  std::string placed_uuid;
  std::string original_placed_uuid;
  std::uint32_t record_version{0};

  // Span of the record BODY (the outer u64 body length is not included).
  std::shared_ptr<const std::vector<std::uint8_t>> raw_storage;
  std::size_t raw_body_offset{0};
  std::size_t raw_body_length{0};

  Rect cache_bounds{};
  std::uint32_t cache_depth{0};
  std::uint32_t cache_max_channels{0};
  bool cache_layout_valid{false};

  bool mask_present{false};
  bool mask_decoded{false};
  std::optional<SmartFilterEffectsMask> mask;

  // data_supported covers the verified version-1 record/cache layout and, when
  // present, a decoded 8-bit raw/PackBits mask. Association is a separate gate:
  // duplicate or missing placed ids must fail closed even if each body parses.
  bool data_supported{false};
  bool association_unique{false};

  [[nodiscard]] bool semantic_supported() const noexcept {
    return data_supported && association_unique;
  }
};

struct SmartFilterEffectsBlock {
  std::string key{"FEid"};
  bool long_length{false};
  // Position among all document-global tagged blocks. SIZE_MAX denotes an
  // authored block that is emitted after preserved source blocks.
  std::size_t original_global_index{SIZE_MAX};
  std::uint32_t version{3};
  std::vector<SmartFilterEffectsRecord> records;

  // Exact original block payload. It is emitted verbatim until records change.
  std::shared_ptr<const std::vector<std::uint8_t>> original_payload;
  // A broken outer version/record-length walk is opaque. No record is edited or
  // adopted into it and serialization always returns original_payload.
  bool opaque{false};
};

struct SmartFilterEffectsStore {
  std::vector<SmartFilterEffectsBlock> blocks;

  [[nodiscard]] bool empty() const noexcept;

  // Adds a parsed block and recomputes association uniqueness across every
  // FEid/FXid block (duplicates can occur across blocks, not only within one).
  void add_block(SmartFilterEffectsBlock block);

  // Returns a record only when exactly one non-empty placed-id match exists
  // throughout the store. Duplicate Photoshop associations fail closed.
  [[nodiscard]] const SmartFilterEffectsRecord *
  find_unique(std::string_view placed_uuid) const noexcept;
  [[nodiscard]] SmartFilterEffectsRecord *
  find_unique(std::string_view placed_uuid) noexcept;

  // Clones a uniquely-associated raw record in its current block and changes
  // only its placed id when serialized. Returns false on ambiguity/collision.
  bool clone_rekey(std::string_view source_placed_uuid,
                   std::string new_placed_uuid);

  // Carries a raw record into this store, preserving its FEid/FXid dialect. The
  // destination id must be non-empty and unused. The source backing storage and
  // decoded mask samples stay shared.
  bool adopt(const SmartFilterEffectsRecord &source,
             std::string new_placed_uuid);

  // Inserts or replaces one authored record by its placed id. The supplied
  // record must already own a complete native record body. Existing parsed
  // blocks keep their dialect and ordering; a new record uses a Photoshop-style
  // version-3 FEid block. Opaque blocks and duplicate associations fail closed.
  bool upsert_authored(SmartFilterEffectsRecord record);

  // Removes exactly one uniquely-associated record and drops its block if that
  // was the final record. Ambiguous associations are left untouched.
  bool remove(std::string_view placed_uuid);
};

} // namespace patchy
