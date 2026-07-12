#pragma once

#include "core/pattern_resource.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

// Reader for standalone Adobe Photoshop .pat pattern-library files. The file
// envelope is '8BPT', version 1, followed by a counted list of pattern records.
// Record pixels use the same Virtual Memory Array layout as PSD Patt/Pat2/Pat3
// blocks, so their channel decode is shared with psd_patterns.
namespace patchy {
class CmykToRgbTransform;
}

namespace patchy::psd {

struct PatReadResult {
  // Standalone imports have no document raw block that can preserve them, so
  // resources are returned with PatternProvenance::Authored. A document that
  // adopts one will therefore embed it when the pattern is referenced.
  std::vector<PatternResource> patterns;
  // Per-pattern skips (unsupported, oversize, or undecodable) plus repaired
  // missing/invalid ids that could not round-trip through the user library.
  std::vector<std::string> warnings;
};

// Parses an in-memory .pat file. Returns std::nullopt and sets `error` when the
// file as a whole is unusable (bad header, unsafe count, truncation before any
// usable record, or no supported patterns). Once at least one record has been
// decoded, an unreadable trailing record is reported as a warning and the
// successfully decoded prefix is returned. Trailing 8BIMphry hierarchy data is
// deliberately ignored. CMYK uses `cmyk_icc` when supplied and the existing
// naive Photoshop ink mix otherwise.
[[nodiscard]] std::optional<PatReadResult> read_pat(
    std::span<const std::uint8_t> bytes, std::string& error,
    const CmykToRgbTransform* cmyk_icc = nullptr);

}  // namespace patchy::psd
