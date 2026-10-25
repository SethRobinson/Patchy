#pragma once

#include <string>

namespace patchy {

// Shape of a generated 8-4-4-4-12 lowercase hex identifier.
enum class RandomUuidShape {
  // Every nibble random: pattern resource ids.
  PlainHex,
  // The first nibble of the fourth group carries the RFC 4122 variant bits
  // (8 to b): Photoshop's placed-instance ids have that form even though their
  // version nibble is not fixed, and Smart Object ids match it.
  Rfc4122Variant,
};

// A fresh random identifier from one process-wide generator. Safe to call from
// any thread: the file importers (Affinity, PDF, SVG, ASL, PAT) and the UI all
// generate ids, sometimes concurrently.
[[nodiscard]] std::string generate_random_uuid(RandomUuidShape shape);

}  // namespace patchy
