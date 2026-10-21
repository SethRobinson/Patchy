#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace patchy::hdr {

// Decodes PQ (SMPTE ST 2084) and HLG (ARIB STD-B67, ITU-R BT.2100) signals into Patchy's
// 32-bit model: linear light, sRGB primaries, 1.0 at SDR reference white. Shared by the
// readers that receive transfer-encoded HDR samples (AVIF, HEIF on macOS); libjxl does the
// same conversion itself for JPEG XL. Decode only: nothing in Patchy encodes PQ or HLG.
// Licensing record: docs/patent-research-formats.md ("PQ and HLG decoding").

// ITU-R BT.2408 reference white: the luminance that becomes 1.0 in a 32-bit document.
inline constexpr double kSdrReferenceWhiteNits = 203.0;
// Nominal peak of the BT.2100 HLG reference display. At 1000 nits the reference OOTF has
// gamma 1.2 and puts the 75% HLG signal on 203 nits, BT.2408's conversion point.
inline constexpr double kHlgNominalPeakNits = 1000.0;

enum class Transfer { Pq, Hlg };

// SMPTE ST 2084 EOTF: a 0..1 signal to absolute luminance in nits (0..10000).
[[nodiscard]] double pq_signal_to_nits(double signal);
// BT.2100 HLG inverse OETF: a 0..1 signal to normalized scene light (0..1).
[[nodiscard]] double hlg_signal_to_scene(double signal);

// CIE xy chromaticities of a set of RGB primaries and its white point.
struct Chromaticities {
  double red_x{0.0};
  double red_y{0.0};
  double green_x{0.0};
  double green_y{0.0};
  double blue_x{0.0};
  double blue_y{0.0};
  double white_x{0.0};
  double white_y{0.0};
};

inline constexpr Chromaticities kBt709Primaries{0.640, 0.330, 0.300, 0.600, 0.150, 0.060, 0.3127, 0.3290};
inline constexpr Chromaticities kBt2020Primaries{0.708, 0.292, 0.170, 0.797, 0.131, 0.046, 0.3127, 0.3290};
inline constexpr Chromaticities kDisplayP3Primaries{0.680, 0.320, 0.265, 0.690, 0.150, 0.060, 0.3127, 0.3290};

// Row-major 3x3 matrix from linear RGB in `source` primaries to linear sRGB (BT.709, D65),
// Bradford-adapted when the white points differ. Out-of-gamut colors come out negative and
// are kept: 32-bit documents are unbounded.
[[nodiscard]] std::array<double, 9> linear_to_srgb_matrix(const Chromaticities& source);

// Converts RGBA samples whose color channels carry a PQ or HLG signal (full range, 0..65535
// meaning 0..1) in `source` primaries into tightly packed linear sRGB float RGBA, 1.0 at
// SDR reference white. Alpha is linear coverage and scales to 0..1. HLG goes through the
// reference OOTF at kHlgNominalPeakNits with the luminance weights of `source`.
void signal_rgba16_to_linear_srgb(const std::uint16_t* rgba16, std::size_t pixel_count, Transfer transfer,
                                  const Chromaticities& source, float* rgba_float);

}  // namespace patchy::hdr
