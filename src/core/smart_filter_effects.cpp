#include "core/smart_filter_effects.hpp"

#include <algorithm>
#include <utility>

namespace patchy {

namespace {

[[nodiscard]] bool
record_has_raw_body(const SmartFilterEffectsRecord &record) noexcept {
  return record.raw_storage != nullptr &&
         record.raw_body_offset <= record.raw_storage->size() &&
         record.raw_body_length <=
             record.raw_storage->size() - record.raw_body_offset &&
         !record.original_placed_uuid.empty();
}

[[nodiscard]] bool
store_contains_placed_uuid(const SmartFilterEffectsStore &store,
                           std::string_view placed_uuid) noexcept {
  if (placed_uuid.empty()) {
    return false;
  }
  for (const auto &block : store.blocks) {
    for (const auto &record : block.records) {
      if (record.placed_uuid == placed_uuid) {
        return true;
      }
    }
  }
  return false;
}

void refresh_association_uniqueness(SmartFilterEffectsStore &store) {
  const bool has_opaque_block = std::any_of(
      store.blocks.begin(), store.blocks.end(),
      [](const SmartFilterEffectsBlock& block) { return block.opaque; });
  for (auto &block : store.blocks) {
    for (auto &record : block.records) {
      record.association_unique = false;
      if (has_opaque_block || record.placed_uuid.empty()) {
        continue;
      }
      std::size_t matches = 0;
      for (const auto &candidate_block : store.blocks) {
        for (const auto &candidate : candidate_block.records) {
          if (candidate.placed_uuid == record.placed_uuid) {
            ++matches;
          }
        }
      }
      record.association_unique = matches == 1U;
    }
  }
}

} // namespace

bool SmartFilterEffectsStore::empty() const noexcept { return blocks.empty(); }

void SmartFilterEffectsStore::add_block(SmartFilterEffectsBlock block) {
  blocks.push_back(std::move(block));
  refresh_association_uniqueness(*this);
}

const SmartFilterEffectsRecord *SmartFilterEffectsStore::find_unique(
    std::string_view placed_uuid) const noexcept {
  if (placed_uuid.empty() ||
      std::any_of(blocks.begin(), blocks.end(),
                  [](const SmartFilterEffectsBlock& block) {
                    return block.opaque;
                  })) {
    return nullptr;
  }
  const SmartFilterEffectsRecord *found = nullptr;
  for (const auto &block : blocks) {
    for (const auto &record : block.records) {
      if (record.placed_uuid != placed_uuid) {
        continue;
      }
      if (found != nullptr) {
        return nullptr;
      }
      found = &record;
    }
  }
  return found;
}

SmartFilterEffectsRecord *
SmartFilterEffectsStore::find_unique(std::string_view placed_uuid) noexcept {
  return const_cast<SmartFilterEffectsRecord *>(
      std::as_const(*this).find_unique(placed_uuid));
}

bool SmartFilterEffectsStore::clone_rekey(std::string_view source_placed_uuid,
                                          std::string new_placed_uuid) {
  if (source_placed_uuid.empty() || new_placed_uuid.empty() ||
      store_contains_placed_uuid(*this, new_placed_uuid) ||
      std::any_of(blocks.begin(), blocks.end(),
                  [](const SmartFilterEffectsBlock& block) {
                    return block.opaque;
                  })) {
    return false;
  }

  std::size_t source_block_index = 0;
  std::size_t source_record_index = 0;
  std::size_t matches = 0;
  for (std::size_t block_index = 0; block_index < blocks.size();
       ++block_index) {
    const auto &block = blocks[block_index];
    if (block.opaque) {
      continue;
    }
    for (std::size_t record_index = 0; record_index < block.records.size();
         ++record_index) {
      if (block.records[record_index].placed_uuid == source_placed_uuid) {
        source_block_index = block_index;
        source_record_index = record_index;
        ++matches;
      }
    }
  }
  if (matches != 1U ||
      !record_has_raw_body(
          blocks[source_block_index].records[source_record_index])) {
    return false;
  }

  // Copy before push_back: growing the vector can invalidate the source
  // reference.
  auto clone = blocks[source_block_index].records[source_record_index];
  clone.placed_uuid = std::move(new_placed_uuid);
  clone.association_unique = true;
  auto &block = blocks[source_block_index];
  block.original_payload.reset();
  // Photoshop writes a duplicated instance's cache directly after the source
  // record. Keep that ordering: Photoshop rejects otherwise-valid FEid blocks
  // when a copied record is merely appended after unrelated instances.
  block.records.insert(
      block.records.begin() +
          static_cast<std::ptrdiff_t>(source_record_index + 1U),
      std::move(clone));
  refresh_association_uniqueness(*this);
  return true;
}

bool SmartFilterEffectsStore::adopt(const SmartFilterEffectsRecord &source,
                                    std::string new_placed_uuid) {
  if (new_placed_uuid.empty() ||
      store_contains_placed_uuid(*this, new_placed_uuid) ||
      !record_has_raw_body(source) ||
      std::any_of(blocks.begin(), blocks.end(),
                  [](const SmartFilterEffectsBlock& block) {
                    return block.opaque;
                  })) {
    return false;
  }
  if ((source.source_block_key != "FEid" &&
       source.source_block_key != "FXid") ||
      source.source_block_version < 1U || source.source_block_version > 3U) {
    return false;
  }

  SmartFilterEffectsBlock *target = nullptr;
  std::optional<std::size_t> source_record_index;
  std::size_t source_matches = 0;
  for (auto &block : blocks) {
    if (block.opaque || block.key != source.source_block_key ||
        block.version != source.source_block_version) {
      continue;
    }
    if (target == nullptr) {
      target = &block;
    }
    for (std::size_t record_index = 0; record_index < block.records.size();
         ++record_index) {
      if (block.records[record_index].placed_uuid != source.placed_uuid) {
        continue;
      }
      target = &block;
      source_record_index = record_index;
      ++source_matches;
    }
  }
  if (source_matches > 1U) {
    return false;
  }
  if (target == nullptr) {
    SmartFilterEffectsBlock block;
    block.key = source.source_block_key;
    block.long_length = source.source_long_length;
    block.version = source.source_block_version;
    blocks.push_back(std::move(block));
    target = &blocks.back();
  }

  auto adopted = source;
  adopted.source_block_key = target->key;
  adopted.source_block_version = target->version;
  adopted.source_long_length = target->long_length;
  adopted.placed_uuid = std::move(new_placed_uuid);
  adopted.association_unique = true;
  target->original_payload.reset();
  if (source_record_index.has_value()) {
    // Copy/Paste within one document reaches adopt() rather than clone_rekey().
    // Photoshop requires the duplicated cache immediately after its source
    // record, so retain the same ordering rule in both paths.
    target->records.insert(
        target->records.begin() + static_cast<std::ptrdiff_t>(*source_record_index + 1U),
        std::move(adopted));
  } else {
    target->records.push_back(std::move(adopted));
  }
  refresh_association_uniqueness(*this);
  return true;
}

bool SmartFilterEffectsStore::upsert_authored(
    SmartFilterEffectsRecord record) {
  if (record.placed_uuid.empty() || !record_has_raw_body(record) ||
      std::any_of(blocks.begin(), blocks.end(),
                  [](const SmartFilterEffectsBlock& block) {
                    return block.opaque;
                  })) {
    return false;
  }

  std::size_t found_block = 0;
  std::size_t found_record = 0;
  std::size_t matches = 0;
  for (std::size_t block_index = 0; block_index < blocks.size();
       ++block_index) {
    for (std::size_t record_index = 0;
         record_index < blocks[block_index].records.size(); ++record_index) {
      if (blocks[block_index].records[record_index].placed_uuid ==
          record.placed_uuid) {
        found_block = block_index;
        found_record = record_index;
        ++matches;
      }
    }
  }
  if (matches > 1U) {
    return false;
  }

  SmartFilterEffectsBlock* target = nullptr;
  if (matches == 1U) {
    target = &blocks[found_block];
  } else {
    const auto compatible = std::find_if(
        blocks.begin(), blocks.end(), [](const SmartFilterEffectsBlock& block) {
          return !block.opaque && block.key == "FEid" && block.version == 3U &&
                 !block.long_length;
        });
    if (compatible != blocks.end()) {
      target = &*compatible;
    } else {
      SmartFilterEffectsBlock block;
      block.key = "FEid";
      block.version = 3U;
      block.long_length = false;
      blocks.push_back(std::move(block));
      target = &blocks.back();
    }
  }

  record.source_block_key = target->key;
  record.source_block_version = target->version;
  record.source_long_length = target->long_length;
  record.original_placed_uuid = record.placed_uuid;
  record.association_unique = true;
  target->original_payload.reset();
  if (matches == 1U) {
    target->records[found_record] = std::move(record);
  } else {
    target->records.push_back(std::move(record));
  }
  refresh_association_uniqueness(*this);
  return true;
}

bool SmartFilterEffectsStore::remove(std::string_view placed_uuid) {
  if (placed_uuid.empty() ||
      std::any_of(blocks.begin(), blocks.end(),
                  [](const SmartFilterEffectsBlock& block) {
                    return block.opaque;
                  })) {
    return false;
  }
  std::size_t found_block = 0;
  std::size_t found_record = 0;
  std::size_t matches = 0;
  for (std::size_t block_index = 0; block_index < blocks.size();
       ++block_index) {
    if (blocks[block_index].opaque) {
      continue;
    }
    for (std::size_t record_index = 0;
         record_index < blocks[block_index].records.size(); ++record_index) {
      if (blocks[block_index].records[record_index].placed_uuid ==
          placed_uuid) {
        found_block = block_index;
        found_record = record_index;
        ++matches;
      }
    }
  }
  if (matches != 1U) {
    return false;
  }

  auto &block = blocks[found_block];
  block.records.erase(block.records.begin() +
                      static_cast<std::ptrdiff_t>(found_record));
  block.original_payload.reset();
  if (block.records.empty()) {
    blocks.erase(blocks.begin() + static_cast<std::ptrdiff_t>(found_block));
  }
  refresh_association_uniqueness(*this);
  return true;
}

} // namespace patchy
