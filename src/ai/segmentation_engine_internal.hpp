#pragma once

// Engine internals shared by the ONNX Runtime engine (desktop) and the web engine (wasm):
// how one prompt becomes decoder inputs and which decoder output is chosen. Both engines
// must solve identically, so this is the only place either rule lives. Read the patent
// boundary in segmentation_engine.hpp before changing anything here.

#include "ai/segmentation_engine.hpp"

#include <array>
#include <cstdint>

namespace patchy::ai {

// The multimask decoder returns four masks with predicted IoUs: index 0 is SAM's
// single-mask output, 1-3 the three alternatives a prompt can mean. A point picks among
// 1-3 (SAM's own rule for an ambiguous single point). A box picks among all four: MobileSAM's
// single-mask output degrades on a loose box (it pulled in a whole fridge behind a boy at
// predicted IoU 0.66 while alternative 3 was the boy at 0.96), so the decoder's own
// confidence decides. Either way it is one choice among the outputs of one run.
inline constexpr std::int64_t kDecoderMaskCount = 4;
inline constexpr std::int64_t kDecoderLowResSize = 256;

// Pinned by scripts/setup-ai-deps.* (docs/ai-selection-models.md lists provenance and licenses):
// the conversions made by scripts/ai/quantize_models.py. MobileSAM (int8) is the Standard
// model, bundled with desktop builds and the only one in the web build (a much smaller
// first-use download there). EfficientViT-SAM XL0 (float16 weights) is the High Quality model:
// a desktop-only download, used once present and enabled (SamModel in segmentation_engine.hpp).
// Both decoders are SAM's: same prompts, four masks with predicted IoUs, no mask input.
inline constexpr const char* kEfficientSamEncoderFile = "efficientvit_sam_xl0_encoder_fp16.onnx";
inline constexpr const char* kEfficientSamDecoderFile = "efficientvit_sam_xl0_decoder_multi_fp16.onnx";
inline constexpr const char* kMobileSamEncoderFile = "mobile_sam_image_encoder_int8.onnx";
inline constexpr const char* kMobileSamDecoderFile = "sam_mask_decoder_multi_int8.onnx";
inline constexpr const char* kSubjectModelFile = "birefnet_lite_general_1024_int8.onnx";

// Exactly one prompt for one decoder run: a single positive point plus SAM's padding point,
// or a single box's two corners. The decoder's mask_input is always zero with
// has_mask_input 0; a previous mask is never fed back.
struct SamDecoderPrompt {
  std::array<float, 4> coords{};
  std::array<float, 2> labels{};
  std::int64_t first_candidate{0};
};

[[nodiscard]] SamDecoderPrompt sam_point_prompt(const SamTransform& transform, double x, double y) noexcept;
[[nodiscard]] SamDecoderPrompt sam_box_prompt(const SamTransform& transform, Rect box) noexcept;

// The output with the highest predicted IoU among first_candidate..kDecoderMaskCount-1.
[[nodiscard]] std::int64_t best_sam_candidate(const float* scores, std::int64_t first_candidate) noexcept;

// Where a HighQuality model file is: the downloaded models folder when the file is there,
// otherwise beside the bundled models.
[[nodiscard]] std::filesystem::path high_quality_model_path(const char* file);

// Each engine's two network steps; segmentation_engine_common.cpp builds the public calls on
// them. encode_frame encodes an image already cropped to the encoded region (the caller sets
// the transform's origin). decode_prompt is one decoder run from one prompt and returns the
// chosen mask in the encoded region's coordinates.
[[nodiscard]] std::shared_ptr<SamEmbedding> encode_frame(const PixelBuffer& rgba8, InferenceCancel* cancel);
[[nodiscard]] AiMask decode_prompt(const SamEmbedding& embedding, const SamDecoderPrompt& prompt);

}  // namespace patchy::ai
