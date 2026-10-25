#pragma once

// The photographic develop stages that work on any RGB image (docs/camera-raw.md,
// "Camera Raw Filter"). Develop Raw runs the tone curve and saturation/vibrance below on
// LibRaw's 16-bit output (formats/raw_tone.hpp re-exports them); the Camera Raw Filter
// runs the whole PhotoDeveloper pipeline on an ordinary layer at its own depth.
//
// Everything is plain double math on fixed tables: deterministic in behavior, but like
// the raw pipeline never byte-pinned in tests beyond the identity and LUT values.

#include "core/pixel_buffer.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

namespace patchy {

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

// The composed shadows -> highlights -> contrast curve at one value in [0, 1]. The lookup
// table below samples exactly this; the Develop Raw dialog's Shift-click auto values model
// the Tone sliders with it.
[[nodiscard]] double tone_curve_value(double x, const ToneParams& params);

// The composed shadows -> highlights -> contrast curve on gamma-encoded sRGB values as a
// 16-bit lookup table. Neutral parameters produce an exact identity table.
[[nodiscard]] std::array<std::uint16_t, 65536> build_tone_lut(const ToneParams& params);

// Saturation/vibrance on interleaved 16-bit RGB triplets, in place. Saturation scales
// distance from Rec.709 luma by 1 + saturation/100; vibrance adds up to vibrance/100 more,
// weighted by how UNsaturated the pixel already is (vivid pixels move less). Both
// -100..100; values of 0 leave pixels untouched.
void apply_color(std::span<std::uint16_t> interleaved_rgb, double saturation, double vibrance);

// The Camera Raw Filter's settings. Temperature and tint are relative (0 keeps the
// layer's colors), not kelvin: an ordinary layer has no camera white point to start from.
struct PhotoDevelopParams {
  double temperature{0.0};  // -100 (cooler, bluer) .. 100 (warmer, yellower)
  double tint{0.0};         // -100 (greener) .. 100 (more magenta)
  double exposure_ev{0.0};  // -5 .. 5 stops, on linear light
  double contrast{0.0};     // -100..100, the tone curve above
  double highlights{0.0};
  double shadows{0.0};
  double saturation{0.0};   // -100..100, apply_color above
  double vibrance{0.0};

  [[nodiscard]] bool is_neutral() const noexcept {
    return temperature == 0.0 && tint == 0.0 && exposure_ev == 0.0 && contrast == 0.0 &&
           highlights == 0.0 && shadows == 0.0 && saturation == 0.0 && vibrance == 0.0;
  }
  friend bool operator==(const PhotoDevelopParams&, const PhotoDevelopParams&) = default;
};

inline constexpr double kPhotoDevelopMaxExposureEv = 5.0;

// Clamps every field to its range; NaN becomes 0.
[[nodiscard]] PhotoDevelopParams normalize_photo_develop_params(PhotoDevelopParams params) noexcept;

// Linear-light R, G, B gains of a relative white balance shift, scaled so white keeps its
// Rec.709 luminance. Temperature trades red against blue, tint green against both.
[[nodiscard]] std::array<double, 3> photo_white_balance_gains(double temperature, double tint) noexcept;

// The temperature and tint (clamped to -100..100) whose gains make a linear-light mean
// color neutral: the gray-world automatic white balance. {0, 0} for a black or empty mean.
[[nodiscard]] std::pair<double, double> photo_auto_white_balance(std::array<double, 3> mean_linear_rgb) noexcept;

// The alpha-weighted mean linear-light color of an RGB(A) buffer at any depth, 1.0 = full.
[[nodiscard]] std::array<double, 3> photo_mean_linear_rgb(const PixelBuffer& pixels);

// One PhotoDevelopParams prepared for repeated application. The pipeline per pixel:
// white balance gains and exposure on linear light, then the tone curve on the sRGB
// encoding, then saturation/vibrance, then back to the buffer's depth. 8 and 16-bit
// buffers run the per-channel stages through three 16-bit tables (the tone stage alone
// is exactly build_tone_lut) and the color stage through apply_color; 32-bit buffers keep
// linear floats, unclamped above 1 (the tone curve continues with slope 1 there). Stages
// at their neutral value are skipped, so neutral settings leave every byte unchanged.
// Alpha is never touched; gray and other non-RGB buffers are left alone.
class PhotoDeveloper {
public:
  explicit PhotoDeveloper(const PhotoDevelopParams& params);

  [[nodiscard]] const PhotoDevelopParams& params() const noexcept { return params_; }
  [[nodiscard]] bool is_identity() const noexcept { return !channel_stage_ && !color_stage_; }

  // Pixels [x_begin, x_end) of row y, in place. Safe to call from several threads on
  // different rows once the buffer is detached (docs/performance.md).
  void apply_row(PixelBuffer& pixels, std::int32_t y, std::int32_t x_begin, std::int32_t x_end) const;
  void apply(PixelBuffer& pixels) const;

  // One 32-bit pixel (linear, 1.0 = full), for tests and the row loop.
  void apply_linear(std::array<float, 3>& rgb) const noexcept;

private:
  [[nodiscard]] double tone_at(double encoded) const noexcept;

  PhotoDevelopParams params_;
  std::array<double, 3> scale_{1.0, 1.0, 1.0};  // white balance gain times exposure
  bool scale_active_{false};
  bool tone_active_{false};
  bool channel_stage_{false};
  bool color_stage_{false};
  std::vector<std::uint16_t> tone_lut_;  // build_tone_lut, when the tone stage is active
  std::array<std::vector<std::uint16_t>, 3> channel_luts_;  // 8/16-bit path, 65536 entries each
};

}  // namespace patchy
