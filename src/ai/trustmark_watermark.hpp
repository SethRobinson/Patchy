#pragma once

// TrustMark scan and embed on Patchy pixels (docs/watermarks.md): the crop, resize and
// residual steps of Adobe's Python reference around the ONNX models in watermark_engine.hpp.
// Qt-free. The models' float math depends on the CPU, so tests check decoded payloads, never
// pixel bytes.
//
// Detection and embedding only: nothing here removes a watermark (docs/legal-constraints.md,
// "Watermark detection").

#include "ai/watermark_engine.hpp"
#include "core/layer.hpp"
#include "core/pixel_buffer.hpp"
#include "core/trustmark_payload.hpp"

#include <string>
#include <vector>

namespace patchy::ai {

struct TrustMarkMark {
  TrustMarkVariant variant{TrustMarkVariant::Q};
  trustmark::Schema schema{trustmark::Schema::Bch5};
  std::vector<std::uint8_t> data_bits;  // the corrected payload
  std::string text;                     // the payload read as TrustMark text, empty when not text
  std::string hex;                      // the payload as hexadecimal ("0x...")
  int corrected_bits{0};
  float confidence{0.0F};  // mean |logit| of the 100 decoder outputs
};

// The least mean |logit| a packet needs to count as a mark, on top of its BCH check. The BCH
// check alone passes on about 2% of unmarked photos per variant (always the weak BCH_3
// schema); marked images score 9 to 11, unmarked ones stayed under 1.3 (docs/watermarks.md).
inline constexpr float kTrustMarkMinimumConfidence = 3.0F;

struct TrustMarkScan {
  bool ran{false};                  // false when the models are unavailable or the image is empty
  std::string unavailable_reason;   // English, when !ran
  std::vector<TrustMarkMark> marks;  // at most one per variant
};

// The part of a width x height image TrustMark reads and marks: the whole image, or the
// centered square when one side is more than twice the other (always for variant P).
[[nodiscard]] Rect trustmark_region(int width, int height, TrustMarkVariant variant) noexcept;

// `region` of 8-bit RGB or RGBA `image` resized to size x size as PIL's bilinear resize does
// (a triangle filter widened by the reduction, so large images are averaged, not sampled),
// as an NCHW float tensor in [-1, 1].
[[nodiscard]] std::vector<float> trustmark_model_input(const PixelBuffer& image, Rect region, int size);

// Runs the Q, P and C decoders on an 8-bit RGB or RGBA image and keeps every packet whose
// BCH code checks (decode_packet) with at least kTrustMarkMinimumConfidence. Throws
// InferenceCancelled on cancel.
[[nodiscard]] TrustMarkScan scan_trustmark(const PixelBuffer& image, InferenceCancel* cancel = nullptr);

// TrustMark's default strength is 1.0; Adobe reports reliable decoding down to about 0.7 in
// light processing and uses 1.5 for print.
inline constexpr float kTrustMarkDefaultStrength = 1.0F;

// Adds the residual of one encoder run to `region` of `image` (8-bit RGB or RGBA, alpha
// kept), as the Python reference's encode does: residual = clamp(stego) - cover, minus its
// per-channel mean, bilinearly upsampled to the region, scaled by `strength`, added and
// clipped, with the region's border feathered over 1% of its shorter side. Exposed for
// tests; embed_trustmark_q is the whole step.
void apply_trustmark_residual(PixelBuffer& image, Rect region, const std::vector<float>& cover,
                              const std::vector<float>& stego, float strength);

// Embeds `packet` into `image` with the variant Q encoder. Throws std::runtime_error when the
// models are unavailable and InferenceCancelled on cancel.
void embed_trustmark_q(PixelBuffer& image, const trustmark::Packet& packet, float strength,
                       InferenceCancel* cancel = nullptr);

}  // namespace patchy::ai
