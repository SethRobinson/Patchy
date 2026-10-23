#pragma once

// TrustMark watermark inference (docs/watermarks.md): Adobe's ONNX decoders for variants Q,
// P and C and the variant Q encoder, all from the one optional "watermark models" download.
// Runs on the same ONNX Runtime as automatic selection; the implementation lives in
// segmentation_engine_onnx.cpp, the only translation unit that includes ONNX Runtime. Builds
// without it (and the web build) report the models unavailable.

#include "ai/segmentation_engine.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace patchy::ai {

// TrustMark variants. A mark made with one variant only decodes with that variant's decoder.
enum class TrustMarkVariant : std::uint8_t { Q, P, C };

[[nodiscard]] std::string_view trustmark_variant_name(TrustMarkVariant variant) noexcept;  // "Q", "P", "C"
// The decoder's square input size: 224 for P, 256 otherwise.
[[nodiscard]] int trustmark_decoder_input_size(TrustMarkVariant variant) noexcept;
constexpr int kTrustMarkEncoderInputSize = 256;
constexpr int kTrustMarkPacketBits = 100;

// File names in the downloaded models folder (never renamed: the uninstaller and
// Preferences delete exactly these).
[[nodiscard]] const std::vector<std::string_view>& trustmark_model_files();

// True when this build can run the watermark models (ONNX Runtime loads), whether or not
// they are downloaded.
[[nodiscard]] bool trustmark_runtime_available();
// True when ONNX Runtime loads and every watermark model file is in the downloaded models
// folder. Only that folder is searched, never the bundled models: packages never ship
// Adobe's models (docs/legal-constraints.md).
[[nodiscard]] bool trustmark_models_available();
// English reason when unavailable; empty when available.
[[nodiscard]] std::string trustmark_unavailable_reason();

// One decoder run on an NCHW float image in [-1, 1] of size input_size x input_size: the
// 100 packet logits (a bit is 1 when its logit is positive).
[[nodiscard]] std::array<float, kTrustMarkPacketBits> trustmark_decode_logits(TrustMarkVariant variant,
                                                                             const std::vector<float>& image,
                                                                             InferenceCancel* cancel = nullptr);

// One variant Q encoder run: the 256 x 256 NCHW cover in [-1, 1] and the 100 packet bits in,
// the watermarked 256 x 256 NCHW image out.
[[nodiscard]] std::vector<float> trustmark_encode_q(const std::vector<float>& cover,
                                                    const std::array<float, kTrustMarkPacketBits>& packet,
                                                    InferenceCancel* cancel = nullptr);

// Frees the watermark sessions (before their files are deleted; they reload on next use).
void release_trustmark_models();

}  // namespace patchy::ai
