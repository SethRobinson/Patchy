#pragma once

#include "core/layer.hpp"

#include <cstdint>
#include <string_view>
#include <vector>

// The lfx2 layer-effects descriptor conversions, implemented in
// psd_document_io.cpp next to their per-effect helpers and pinned against
// Photoshop 2026 (docs/ps-compat.md). Exposed so the standalone .asl style
// codec (psd/asl_io.hpp) shares the exact PSD conversion: an .asl style's
// 'Lefx' object has the same shape as the lfx2 root descriptor.
namespace patchy {
class CmykToRgbTransform;
}

namespace patchy::psd {

struct DescriptorObject;

// Parsed effects descriptor (lfx2 root / .asl 'Lefx' object) -> modeled style.
// Any descriptor surprise yields an empty style (historical lfx2 semantics).
// CMYK effect colors convert through `cmyk_icc` when non-null, the naive
// Photoshop ink mix otherwise.
[[nodiscard]] LayerStyle layer_style_from_lefx_descriptor(const DescriptorObject& root,
                                                          const CmykToRgbTransform* cmyk_icc);

// Full native lfx2 payload: u32 version 0, u32 descriptor version 16, then the
// serialized effects descriptor (blend modes as stringIDs, PS-shape-sensitive
// GrFl/FrFX/ebbl layouts). The bytes after the 8-byte header are exactly what
// an .asl 'Lefx' object value embeds.
[[nodiscard]] std::vector<std::uint8_t> photoshop_lfx2_layer_style_payload(const LayerStyle& style);

// 'BlnM' enum text -> BlendMode. Accepts modern stringIDs ("multiply") and
// legacy 4-char codes; unknown values fall back to Normal.
[[nodiscard]] BlendMode blend_mode_from_lfx2_enum(std::string_view value);

// BlendMode -> the full stringID form Photoshop requires (it silently reads
// 4-char codes as Normal).
[[nodiscard]] std::string_view blend_mode_lfx2_string(BlendMode mode);

}  // namespace patchy::psd
