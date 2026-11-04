#pragma once

// Automatic selection inference (docs/ai-selection.md): MobileSAM for the Object Selection
// tool (EfficientViT-SAM XL0 instead once it is downloaded and enabled; see SamModel) and
// BiRefNet for Select > Subject, all on ONNX Runtime's CPU provider. The threaded wasm app
// runs only MobileSAM, in the page's onnxruntime-web (segmentation_engine_web.cpp); there
// every inference call must run off the main thread.
// Builds without either (PATCHY_ENABLE_AI_SELECTION off, wasm-core, single-threaded wasm)
// link a stub that reports the feature unavailable, so callers never need their own #if.
//
// Patent boundary (binding; see the "Automatic selection" bullet in
// docs/legal-constraints.md): every call below is ONE network solve from ONE prompt.
// Never send a negative point (US 11314982), never combine a box with points in one solve
// (US 11568627), never feed a previous mask back into the decoder (US 11676279: MobileSAM's
// mask_input is always zero with has_mask_input 0, and the EfficientViT-SAM decoder has no
// mask input), and Select Subject runs one pass over the whole image with no
// network-located crop or second pass (US 11244195). Zoom-to-box encodes the region
// sam_box_region derives from the user's own box by fixed geometry, once per box, and
// decodes that box once: never a region a network located, never a second pass over a
// result. Callers combine results with plain selection booleans only.

#include "ai/ai_selection_math.hpp"

#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace patchy::ai {

// Thrown by an inference call whose InferenceCancel was requested.
class InferenceCancelled final : public std::runtime_error {
public:
  InferenceCancelled();
};

// Thrown by an inference call when an allocation failed, inside ONNX Runtime or while
// loading a model. Select Subject's model peaks at several GB, so this is a real outcome on
// a busy machine; nothing is left half-done, and the same call can succeed once memory is
// free.
class InferenceOutOfMemory final : public std::runtime_error {
public:
  InferenceOutOfMemory();
};

// True when an ONNX Runtime error message reports a failed allocation (it reports a
// std::bad_alloc thrown inside a kernel as text, such as "... Status Message: bad allocation").
[[nodiscard]] bool is_out_of_memory_message(std::string_view message) noexcept;
// True for InferenceOutOfMemory and std::bad_alloc: what callers turn into the
// out-of-memory message instead of a raw diagnostic.
[[nodiscard]] bool is_out_of_memory(const std::exception& error) noexcept;

// Cancels a running inference from any thread. One object serves one call at a time.
class InferenceCancel {
public:
  void request();
  [[nodiscard]] bool requested() const noexcept;

  // Engine internals: the running call registers how to terminate its ONNX Runtime run,
  // so a request stops the run instead of waiting for it to finish. Attaching after a
  // request terminates at once.
  void attach(std::function<void()> terminate);
  void detach();

private:
  mutable std::mutex mutex_;
  std::atomic<bool> requested_{false};
  std::function<void()> terminate_;
};

// The models directory Patchy ships ("ai-models" beside the executable, or the bundle's
// Resources). Set once at startup, before the first call; tests point it at the build's
// copy. Thread-safe.
void set_model_directory(const std::filesystem::path& directory);
[[nodiscard]] std::filesystem::path model_directory();

// True when this build has ONNX Runtime and an Object Selection model's files are present.
[[nodiscard]] bool segmentation_available();
// English diagnostic for logs and script errors when unavailable; empty when available.
[[nodiscard]] std::string segmentation_unavailable_reason();

// True when this build ships Select Subject's model and it is present. Select Subject,
// Select Background and Remove Background need it; Object Selection needs only
// segmentation_available(). Off unless CMake's PATCHY_ENABLE_SELECT_SUBJECT is on: the
// model is held back pending patent review (US 12051225, docs/legal-constraints.md).
[[nodiscard]] bool subject_selection_available();

// The two Object Selection models (docs/ai-selection-models.md). Standard is MobileSAM, bundled
// with every build that has automatic selection (and the only one in the web build).
// HighQuality is EfficientViT-SAM XL0, a 234 MB download desktop users opt into; it runs
// only when its files are present and the user's setting allows it. Which model runs is
// the user's choice, never derived from image content (patent boundary above: no automatic
// choice between models).
enum class SamModel {
  Standard,
  HighQuality
};

// Where downloaded models live (the per-user app-data folder; empty until set). HighQuality's
// files are looked up there first, then beside the bundled models (a build tree or a
// packager may stage them there). Thread-safe.
void set_downloaded_model_directory(const std::filesystem::path& directory);
[[nodiscard]] std::filesystem::path downloaded_model_directory();
// True when HighQuality's files are present. Always false in the web build.
[[nodiscard]] bool high_quality_model_available();
// The user's setting: whether HighQuality runs once it is available (default true).
void set_high_quality_model_enabled(bool enabled);
[[nodiscard]] bool high_quality_model_enabled();
// The model the next sam_encode uses: HighQuality when available and enabled.
[[nodiscard]] SamModel active_sam_model();

// The SAM image embedding of one image or one region of it, and the model that made it
// (decoding uses that model's decoder). Computing it is the expensive step (about 1.5 s for
// EfficientViT-SAM XL0 on a fast desktop CPU, 0.3-1 s for MobileSAM); each prompt
// afterwards is a fast decoder run.
struct SamEmbedding {
  std::vector<float> values;
  SamTransform transform;
  SamModel model{SamModel::Standard};
};

// Encodes an RGBA8 image (composited over white). Throws InferenceCancelled on cancel and
// std::runtime_error when the model cannot run.
[[nodiscard]] std::shared_ptr<const SamEmbedding> sam_encode(const PixelBuffer& rgba8,
                                                             InferenceCancel* cancel = nullptr);

// Encodes only `region` (document coordinates, clamped to the image) at the encoder's full
// resolution: zoom-to-box, with the region from sam_box_region. Prompts and results stay in
// document coordinates.
[[nodiscard]] std::shared_ptr<const SamEmbedding> sam_encode_region(const PixelBuffer& rgba8, Rect region,
                                                                    InferenceCancel* cancel = nullptr);

// One positive point (document coordinates) -> the object under it: one decoder run, the
// multimask output with the highest predicted IoU.
[[nodiscard]] AiMask sam_select_point(const SamEmbedding& embedding, double x, double y);

// One box (document coordinates) -> the object inside it: one decoder run, the output
// (single-mask or alternative) with the highest predicted IoU, clipped to the box.
[[nodiscard]] AiMask sam_select_box(const SamEmbedding& embedding, Rect box);

// The main subject's soft matte at document resolution: one BiRefNet pass over the whole
// image (stretched to 1024 square), upsampled once. Throws std::runtime_error when
// subject_selection_available() is false.
[[nodiscard]] AiMask subject_matte(const PixelBuffer& rgba8, InferenceCancel* cancel = nullptr);

// Progress of a model download the running inference waits on, in permille, or -1 when
// nothing is downloading. Only the web engine downloads on first use; desktop builds ship
// their bundled models beside the executable (the High Quality model's download is the app's
// own, before any run) and always return -1.
[[nodiscard]] int model_download_permille() noexcept;

// Frees the loaded sessions (they reload on next use). The subject model is the large one
// (about 220 MB of weights, about 2 s to load): it stays loaded after a run so a second
// Select Subject is faster, and the app frees it after a while idle.
void release_models();
void release_subject_model();
// Frees only the High Quality model's sessions (before its files are deleted). A run in
// progress keeps its own reference and finishes normally.
void release_high_quality_model();

}  // namespace patchy::ai
