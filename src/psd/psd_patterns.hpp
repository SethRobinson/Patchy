#pragma once

#include "core/pattern_resource.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

// Binary codec for the PSD document-global pattern blocks ('Patt'/'Pat2'/'Pat3'):
// each block holds one or more patterns as {length, version=1, image mode, point,
// unicode name, pascal-string id, [indexed color table], Virtual Memory Array List},
// 4-byte padded per pattern. Layout pinned against Photoshop 2026 (27.8) captures —
// see docs/ps-compat.md "Pattern data blocks". Color channels occupy the first VMA
// slots; the pattern's transparency plane lives in the LAST slot (max channels + 2
// slots total; PS declares 24 max channels regardless of mode).
//
// Preservation contract: imported blocks stay raw in
// DocumentMetadata::unknown_psd_resources and are re-emitted verbatim; this codec
// only DECODES them into the PatternStore for rendering, and SERIALIZES the
// Patchy-authored resources the raw blocks do not cover into one new 'Patt' block.
namespace patchy {
class CmykToRgbTransform;
}

namespace patchy::psd {

// Decodes every pattern in a block payload into RGBA8 resources (provenance
// ImportedRaw). Undecodable patterns are skipped — the raw block preserves them —
// and a malformed payload never throws out of this function. Gray/Indexed/RGB/CMYK
// at 8/16-bit and compression 0 (raw) / 1 (PackBits with a per-row count table)
// are supported; CMYK converts through `cmyk_icc` when non-null, the naive
// Photoshop ink mix otherwise.
[[nodiscard]] std::vector<PatternResource> parse_patterns_block(
    std::span<const std::uint8_t> payload, const CmykToRgbTransform* cmyk_icc);

// Header-only scan: the pattern ids present in a raw block, without pixel decode.
// The writer uses this to avoid re-embedding ids already covered by preserved raw
// blocks.
[[nodiscard]] std::vector<std::string> pattern_ids_in_block(std::span<const std::uint8_t> payload);

// Serializes resources into one pattern-block payload: version 1, RGB mode, 8-bit,
// raw (compression 0) planar channels — the exact shape PS 27.8 writes for small
// tiles — with the transparency plane written only when some pixel is not opaque.
// Callers pass resources sorted/deduped as desired; output is deterministic.
[[nodiscard]] std::vector<std::uint8_t> serialize_patterns_block(
    std::span<const PatternResource> patterns);

}  // namespace patchy::psd
