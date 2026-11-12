#pragma once

#include "core/pixel_buffer.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

// Select > Color Range (GitHub issue 709, docs/color-range.md): an 8-bit soft selection mask
// from each pixel's own color. Integer math only, so every toolchain gives the same bytes.
// One pass over the image per call; nothing here looks at a pixel's neighbours, detects
// content or groups pixels by position.
namespace patchy {

// Append-only: the tokens below are script and settings identifiers.
enum class ColorRangeMode : std::uint8_t {
  SampledColors,
  Reds,
  Yellows,
  Greens,
  Cyans,
  Blues,
  Magentas,
  Highlights,
  Midtones,
  Shadows
};

inline constexpr int kColorRangeModeCount = 10;
inline constexpr int kMinColorRangeFuzziness = 0;
inline constexpr int kMaxColorRangeFuzziness = 200;
inline constexpr int kDefaultColorRangeFuzziness = 40;
inline constexpr std::size_t kMaxColorRangeSamples = 32;

// A sampled color (display-encoded 8-bit RGB). A `subtract` sample takes its range back out
// of what the other samples select.
struct ColorRangeSample {
  std::uint8_t red{0};
  std::uint8_t green{0};
  std::uint8_t blue{0};
  bool subtract{false};

  friend bool operator==(const ColorRangeSample&, const ColorRangeSample&) = default;
};

struct ColorRangeOptions {
  ColorRangeMode mode{ColorRangeMode::SampledColors};
  // 0..200. Sampled Colors: the RGB distance at which a color stops being selected; colors
  // within half of it are fully selected and the rest fade out linearly (0 = exact matches
  // only). Hue bands and tones: the width of the soft edge past the band (0 = hard edge).
  int fuzziness{kDefaultColorRangeFuzziness};
  // Sampled Colors only; ignored by the other modes.
  std::vector<ColorRangeSample> samples;
  bool invert{false};

  friend bool operator==(const ColorRangeOptions&, const ColorRangeOptions&) = default;
};

// "sampled", "reds", "yellows", "greens", "cyans", "blues", "magentas", "highlights",
// "midtones", "shadows" (permanent).
[[nodiscard]] std::string_view color_range_mode_token(ColorRangeMode mode) noexcept;
[[nodiscard]] std::optional<ColorRangeMode> color_range_mode_from_token(std::string_view token) noexcept;

// The per-pixel rule, prepared once for a set of options:
// - Sampled Colors: for each sample, coverage by the Euclidean RGB distance d to it: 255
//   up to fuzziness / 2, falling linearly to 0 at fuzziness. Added samples combine by
//   maximum; subtract samples scale the result by (255 - their own strongest coverage).
//   The ramp is linear in the distance and each sample stays one point: never a Gaussian
//   or other statistical color model (docs/legal-constraints-selection.md, "Color Range").
// - Reds .. Magentas: the pixel's hue (integer hexcone, 1536 steps a turn) against the
//   band's 60 degree sector: full inside it, fading over fuzziness * 30 / 200 degrees past
//   each side (at the maximum it ends at the neighbouring band's center); times the
//   pixel's chroma (max - min, full from 128 up), so grays are never selected.
// - Shadows, Midtones, Highlights: the pixel's luma (77 R + 150 G + 29 B, /256) against
//   the lower, middle or upper third, fading over fuzziness * 85 / 200 levels past it.
// Every mode then scales by the pixel's alpha (a clear pixel has no color), and Invert
// flips the result last.
class ColorRangeClassifier {
public:
  explicit ColorRangeClassifier(const ColorRangeOptions& options);

  [[nodiscard]] std::uint8_t classify(std::uint8_t red, std::uint8_t green, std::uint8_t blue,
                                      std::uint8_t alpha) const noexcept;
  // `count` straight-alpha RGBA8 pixels in, `count` coverage bytes out.
  void classify_row(const std::uint8_t* rgba, std::size_t count, std::uint8_t* coverage) const noexcept;

private:
  [[nodiscard]] std::uint8_t sampled_coverage(int red, int green, int blue) const noexcept;
  [[nodiscard]] std::uint8_t hue_band_coverage(int red, int green, int blue) const noexcept;
  [[nodiscard]] std::uint8_t tone_coverage(int red, int green, int blue) const noexcept;

  ColorRangeMode mode_{ColorRangeMode::SampledColors};
  bool invert_{false};
  int fuzziness_{0};
  std::vector<ColorRangeSample> add_samples_;
  std::vector<ColorRangeSample> subtract_samples_;
  // Sampled Colors: coverage by squared RGB distance, up to fuzziness squared.
  std::vector<std::uint8_t> distance_ramp_;
};

// The mask of a whole color buffer: gray8, the source's size. `source` is RGB or RGBA at
// any bit depth; 16 and 32-bit pixels are read as their display-encoded 8-bit colors
// (display_rgba8_at), so a 32-bit linear image is compared the way it looks. An empty
// buffer, or one that is not RGB or RGBA, gives an empty mask.
[[nodiscard]] PixelBuffer color_range_mask(const PixelBuffer& source, const ColorRangeOptions& options);

}  // namespace patchy
