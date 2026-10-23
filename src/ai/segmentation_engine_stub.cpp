// Automatic selection in a build without an engine (PATCHY_ENABLE_AI_SELECTION off,
// wasm-core, the single-threaded wasm app): everything reports unavailable.

#include "ai/segmentation_engine.hpp"
#include "ai/segmentation_engine_internal.hpp"
#include "ai/watermark_engine.hpp"

namespace patchy::ai {
namespace {

[[noreturn]] void throw_unavailable() {
  throw std::runtime_error(segmentation_unavailable_reason());
}

}  // namespace

bool segmentation_available() {
  return false;
}

std::string segmentation_unavailable_reason() {
  return "this build of Patchy does not include automatic selection";
}

bool subject_selection_available() {
  return false;
}

std::shared_ptr<SamEmbedding> encode_frame(const PixelBuffer&, InferenceCancel*) {
  throw_unavailable();
}

AiMask decode_prompt(const SamEmbedding&, const SamDecoderPrompt&) {
  throw_unavailable();
}

AiMask subject_matte(const PixelBuffer&, InferenceCancel*) {
  throw_unavailable();
}

int model_download_permille() noexcept {
  return -1;
}

void release_models() {}

void release_subject_model() {}

void release_high_quality_model() {}

std::string trustmark_unavailable_reason() {
  return "this build of Patchy cannot run the watermark models";
}

bool trustmark_models_available() {
  return false;
}

bool trustmark_runtime_available() {
  return false;
}

std::array<float, kTrustMarkPacketBits> trustmark_decode_logits(TrustMarkVariant, const std::vector<float>&,
                                                                InferenceCancel*) {
  throw std::runtime_error(trustmark_unavailable_reason());
}

std::vector<float> trustmark_encode_q(const std::vector<float>&, const std::array<float, kTrustMarkPacketBits>&,
                                      InferenceCancel*) {
  throw std::runtime_error(trustmark_unavailable_reason());
}

void release_trustmark_models() {}

}  // namespace patchy::ai
