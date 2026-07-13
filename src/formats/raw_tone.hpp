#pragma once

#include <array>
#include <cstdint>
#include <span>

namespace patchy::raw {

// Tone/color adjustments applied to LibRaw's 16-bit develop output before the final 8-bit
// bake (LibRaw's own pipeline has no contrast/shadows/highlights/saturation parameters).
// Everything operates on gamma-encoded sRGB values in [0, 65535] with plain double math —
// deterministic in behavior, but like the rest of the raw pipeline never byte-pinned in
// tests (see AGENTS.md determinism notes).

struct ToneParams {
  // All -100..100, 0 = neutral. Shadows lifts (or deepens) a band pivoting around ~0.15
  // with pure black pinned; highlights compresses (or pushes) tones above ~mid gray,
  // deliberately NOT pinning pure white so -100 visibly dims blown areas; contrast is a
  // smoothstep-blend S-curve around 0.5 (its exact inverse for negative values), so no
  // hard clamping is introduced.
  double contrast{0.0};
  double highlights{0.0};
  double shadows{0.0};

  [[nodiscard]] bool is_neutral() const noexcept {
    return contrast == 0.0 && highlights == 0.0 && shadows == 0.0;
  }
};

// The composed shadows -> highlights -> contrast curve as a 16-bit lookup table.
// Neutral parameters produce an exact identity table.
[[nodiscard]] std::array<std::uint16_t, 65536> build_tone_lut(const ToneParams& params);

// Saturation/vibrance on interleaved 16-bit RGB triplets, in place. Saturation scales
// distance from Rec.709 luma by 1 + saturation/100; vibrance adds up to vibrance/100 more,
// weighted by how UNsaturated the pixel already is (vivid pixels move less). Both
// -100..100; values of 0 leave pixels untouched.
void apply_color(std::span<std::uint16_t> interleaved_rgb, double saturation, double vibrance);

}  // namespace patchy::raw
