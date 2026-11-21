#pragma once

#include "core/pattern_resource.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

// Binary codec for the PSD document-global pattern blocks ('Patt'/'Pat2'/'Pat3'):
// each block holds one or more patterns as {length, version=1, image mode, point,
// unicode name, pascal-string id, [indexed color table], Virtual Memory Array List},
// 4-byte padded per pattern. Layout pinned against Photoshop 2026 (27.8) captures —
// see docs/psd-layer-styles.md "Pattern data blocks". Color channels occupy the first VMA
// slots; the pattern's transparency plane lives in the LAST slot (max channels + 2
// slots total; PS declares 24 max channels regardless of mode).
//
// Preservation contract: imported blocks stay raw in
// DocumentMetadata::unknown_psd_resources and are re-emitted verbatim except for
// rejected multi-ink patterns repaired at save (see repair_multichannel_patterns). This codec
// only DECODES them into the PatternStore for rendering, and SERIALIZES the
// Patchy-authored resources the raw blocks do not cover into one new 'Patt' block.
namespace patchy {
class CmykToRgbTransform;
}

namespace patchy::psd {

// Limits for one preset file of patterns, shared by the .asl and .pat readers and the
// libraries that import them (docs/style-presets-patterns.md, "Import limits"). The file
// is read into memory whole and every pattern it holds is decoded to RGBA8 before the
// library stores it, so the three scale together: the pixel budget admits any raw RGB
// file at the size cap, and the plane budget is five planes (CMYK and transparency) of
// that. A 32-bit address space (wasm32) keeps the smaller set.
inline constexpr bool kLargePatternLibraries = sizeof(std::size_t) >= 8;
inline constexpr std::uint64_t kMaxPatternLibraryFileBytes = (kLargePatternLibraries ? 512ULL : 32ULL) << 20U;
inline constexpr std::uint64_t kMaxPatternLibraryPixels = (kLargePatternLibraries ? 256ULL : 16ULL) << 20U;
inline constexpr std::uint64_t kMaxPatternLibraryPlaneSamples = kMaxPatternLibraryPixels * 5U;

// The limits one read applies. Tests pass small ones to reach a cutoff without building
// a file of that size.
struct PatternLibraryLimits {
  std::uint64_t file_bytes{kMaxPatternLibraryFileBytes};
  std::uint64_t pixels{kMaxPatternLibraryPixels};
  std::uint64_t plane_samples{kMaxPatternLibraryPlaneSamples};
};

// A running allowance for one file's patterns. `pixels` bounds the RGBA tiles kept and
// `plane_samples` the channel planes decoded on the way: a record's planes carry their
// own rectangles, so a small pattern can still ask for large planes. A pattern is charged
// when it is admitted to the decode, whether or not its data then decodes, so a run of
// damaged records cannot get past the bound.
struct PatternDecodeBudget {
  std::uint64_t pixels{kMaxPatternLibraryPixels};
  std::uint64_t plane_samples{kMaxPatternLibraryPlaneSamples};
  std::size_t skipped{0};  // patterns left out because an allowance ran out
};

// Decodes every pattern in a block payload into RGBA8 resources (provenance
// ImportedRaw). Undecodable patterns are skipped — the raw block preserves them —
// and a malformed payload never throws out of this function. Gray/Indexed/RGB/CMYK
// at 8/16-bit and compression 0 (raw) / 1 (PackBits with a per-row count table)
// are supported; CMYK converts through `cmyk_icc` when non-null, the naive
// Photoshop ink mix otherwise. With a `budget`, a pattern that does not fit what is
// left of it is skipped and counted there.
[[nodiscard]] std::vector<PatternResource> parse_patterns_block(
    std::span<const std::uint8_t> payload, const CmykToRgbTransform* cmyk_icc,
    PatternDecodeBudget* budget = nullptr);

// Header-only scan: the pattern ids present in a raw block, without pixel decode.
// The writer uses this to avoid re-embedding ids already covered by preserved raw
// blocks.
[[nodiscard]] std::vector<std::string> pattern_ids_in_block(std::span<const std::uint8_t> payload);

// Photoshop rejects mode-7 records with multiple color planes. Replace only
// decodable records of that shape with the RGBA8 appearance Patchy displays,
// retaining the UUID, name and alpha. Valid one-plane mode-7 records and other
// records stay verbatim. Nullopt means no repair; the source is never modified.
[[nodiscard]] std::optional<std::vector<std::uint8_t>> repair_multichannel_patterns(
    std::span<const std::uint8_t> payload);

// Serializes resources into one pattern-block payload: version 1, RGB mode, 8-bit,
// raw (compression 0) planar channels — the exact shape PS 27.8 writes for small
// tiles — with the transparency plane written only when some pixel is not opaque.
// Callers pass resources sorted/deduped as desired; output is deterministic.
[[nodiscard]] std::vector<std::uint8_t> serialize_patterns_block(
    std::span<const PatternResource> patterns);

}  // namespace patchy::psd
