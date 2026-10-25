#pragma once

#include <cstdint>

namespace patchy {

// splitmix64 (Steele, Lea and Flood, 2014): the hash behind Patchy's
// deterministic random draws (gradient noise, exemplar inpainting, the
// randomized Pixelate filters, brush dynamics). Integer-only, so it is
// identical on every toolchain (AGENTS.md: core algorithms are deterministic).
// Callers map the output to a range with an explicit multiply-high or shift,
// never a standard distribution, whose output is implementation-defined.
[[nodiscard]] constexpr std::uint64_t splitmix64(std::uint64_t value) noexcept {
  value += 0x9E3779B97F4A7C15ULL;
  value = (value ^ (value >> 30U)) * 0xBF58476D1CE4E5B9ULL;
  value = (value ^ (value >> 27U)) * 0x94D049BB133111EBULL;
  return value ^ (value >> 31U);
}

}  // namespace patchy
