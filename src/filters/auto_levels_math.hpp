#pragma once

#include "core/adjustment_layer.hpp"

#include <array>
#include <cstdint>
#include <optional>

namespace patchy {

using AutoLevelsHistogram = std::array<std::uint32_t, 256>;

// Shared math for the Image > Adjustments auto commands (Auto Tone,
// Auto Contrast, Auto Color). Both filter execution paths (the legacy
// registry wrapper and the invocation engine) must call these helpers so
// their outputs stay byte-identical.
//
// Legal boundary (docs/legal-constraints.md, Filters): the auto adjustments
// derive only whole-image tonal histograms and means, with fixed constants
// (0.1% clip per end, midtone target 128). No spatial windows, no content
// classification, no content-driven algorithm selection.

// Black/white clip points for one histogram at the fixed 0.1% clip, as a
// LevelsRecord with gamma 100 and full output range. Matches the Levels
// dialog Auto button scan (threshold max(1, total/1000), upward black scan,
// downward white scan bounded by white > black + 1). Returns nullopt when
// either scan exhausts without tripping the threshold; callers treat that
// as identity so constant channels and tiny images pass through unchanged.
[[nodiscard]] std::optional<LevelsRecord> auto_levels_clip_scan(const AutoLevelsHistogram& histogram,
                                                                std::uint64_t total_samples);

// The integer gamma_percent in [10, 999] whose levels curve maps
// normalized_mean closest to 128/255; ties prefer the smaller gamma so the
// result is reproducible. Returns 100 (identity) when normalized_mean falls
// outside (0, 1).
[[nodiscard]] int auto_color_gamma_percent(double normalized_mean);

// Clip scan plus Auto Color's neutral-midtone snap: the channel's whole-image
// mean, normalized into the stretched range, picks the gamma that moves it to
// the midtone target.
[[nodiscard]] std::optional<LevelsRecord> auto_color_channel_record(const AutoLevelsHistogram& histogram,
                                                                    std::uint64_t total_samples);

// 256-entry lookup table for the levels transfer over the given record. The
// transfer is this file's own copy of the deliberately-unshared per-channel
// formula (see the clamp_levels_record note in core/adjustment_layer.hpp),
// matching core's levels_channel rounding.
[[nodiscard]] std::array<std::uint8_t, 256> auto_levels_lut(LevelsRecord record);

// Exact pass-through table for degenerate scans; built by hand rather than
// through levels_channel so identity never depends on floating-point
// round-tripping.
[[nodiscard]] std::array<std::uint8_t, 256> auto_levels_identity_lut();

}  // namespace patchy
