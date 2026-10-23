// Automatic selection on ONNX Runtime's CPU execution provider. This is the only
// translation unit that includes ONNX Runtime headers. Read the patent boundary in
// segmentation_engine.hpp before changing any prompt or decoder input here.
//
// ONNX Runtime is loaded at run time, never linked: a machine where the library cannot
// load (macOS older than the dylib's minimum, a damaged install) still starts Patchy and
// simply reports automatic selection unavailable.

#include "ai/segmentation_engine.hpp"
#include "ai/segmentation_engine_internal.hpp"
#include "ai/watermark_engine.hpp"

#include "core/environment.hpp"
#include "core/worker_budget.hpp"

#define ORT_API_MANUAL_INIT
#include <onnxruntime_cxx_api.h>
#undef ORT_API_MANUAL_INIT

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

#include <algorithm>
#include <array>
#include <cstdint>
#include <new>
#include <optional>
#include <string_view>
#include <vector>

namespace patchy::ai {
namespace {

// The executable's folder, where build trees and the Windows package keep the library.
std::filesystem::path executable_directory() {
#if defined(_WIN32)
  std::vector<wchar_t> buffer(32768);
  const auto length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
  return length == 0 ? std::filesystem::path() : std::filesystem::path(std::wstring(buffer.data(), length)).parent_path();
#elif defined(__APPLE__)
  std::uint32_t size = 0;
  _NSGetExecutablePath(nullptr, &size);
  std::vector<char> buffer(size + 1U, '\0');
  if (_NSGetExecutablePath(buffer.data(), &size) != 0) {
    return {};
  }
  return std::filesystem::path(buffer.data()).parent_path();
#else
  std::error_code error;
  const auto self = std::filesystem::read_symlink("/proc/self/exe", error);
  return error ? std::filesystem::path() : self.parent_path();
#endif
}

using GetApiBaseFunction = const OrtApiBase* (*)();

// Loads the library once and initializes the C++ API from it. The handle is never closed.
struct RuntimeLoad {
  bool loaded{false};
  std::string error;
};

const RuntimeLoad& runtime() {
  static const RuntimeLoad result = [] {
    RuntimeLoad load;
    GetApiBaseFunction get_api_base = nullptr;
    const auto directory = executable_directory();
#if defined(_WIN32)
    // Full path only: System32 can hold an unrelated older onnxruntime.dll.
    const auto library = directory / PATCHY_ONNXRUNTIME_LIBRARY;
    if (auto* module = LoadLibraryExW(library.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH); module != nullptr) {
      get_api_base = reinterpret_cast<GetApiBaseFunction>(
          reinterpret_cast<void*>(GetProcAddress(module, "OrtGetApiBase")));
    }
#else
    std::vector<std::filesystem::path> candidates{directory / PATCHY_ONNXRUNTIME_LIBRARY,
                                                  directory / ".." / "Frameworks" / PATCHY_ONNXRUNTIME_LIBRARY,
                                                  std::filesystem::path(PATCHY_ONNXRUNTIME_LIBRARY)};
    for (const auto& candidate : candidates) {
      if (void* handle = dlopen(candidate.c_str(), RTLD_NOW | RTLD_LOCAL); handle != nullptr) {
        get_api_base = reinterpret_cast<GetApiBaseFunction>(dlsym(handle, "OrtGetApiBase"));
        break;
      }
      const char* message = dlerror();
      load.error = message != nullptr ? message : "dlopen failed";
    }
#endif
    const OrtApi* api = get_api_base != nullptr ? get_api_base()->GetApi(ORT_API_VERSION) : nullptr;
    if (api == nullptr) {
      if (load.error.empty()) {
        load.error = std::string("ONNX Runtime (") + PATCHY_ONNXRUNTIME_LIBRARY + ") could not be loaded";
      } else {
        load.error = "ONNX Runtime could not be loaded: " + load.error;
      }
      return load;
    }
    Ort::InitApi(api);
    load.loaded = true;
    load.error.clear();
    return load;
  }();
  return result;
}

struct OrtState {
  std::mutex mutex;
  // Sessions must not outlive their environment; the state is never destroyed (see
  // ort_state) and release_models() only resets sessions.
  std::optional<Ort::Env> env;
  // Shared: a run holds its own reference, so releasing a slot (release_models, deleting the
  // High Quality model) never frees a session another thread is running.
  std::shared_ptr<Ort::Session> standard_encoder;  // MobileSAM
  std::shared_ptr<Ort::Session> standard_decoder;
  std::shared_ptr<Ort::Session> high_quality_encoder;  // EfficientViT-SAM XL0
  std::shared_ptr<Ort::Session> high_quality_decoder;
  std::shared_ptr<Ort::Session> subject;
  // TrustMark (watermark_engine.hpp), in trustmark_model_files() order.
  std::array<std::shared_ptr<Ort::Session>, 4> trustmark;
};

// Never destroyed: at process exit ONNX Runtime's own globals can be torn down before ours
// (seen on macOS, where ReleaseEnv then aborts on a destroyed mutex), so the environment and
// sessions are left for the OS to reclaim. release_models() frees the sessions on demand.
OrtState& ort_state() {
  static auto* state = new OrtState;
  return *state;
}

Ort::Env& environment(OrtState& state) {
  if (!runtime().loaded) {
    throw std::runtime_error(runtime().error);
  }
  if (!state.env) {
    state.env.emplace(ORT_LOGGING_LEVEL_ERROR, "patchy");
  }
  return *state.env;
}

std::filesystem::path model_path(const char* file) {
  return model_directory() / std::filesystem::path(file);
}

Ort::SessionOptions session_options() {
  Ort::SessionOptions options;
  // ONNX Runtime sizes its pool to the physical cores by default, which measured fastest.
  // PATCHY_RENDER_THREADS still emulates a smaller machine (core/worker_budget.hpp).
  if (environment_variable_is_set("PATCHY_RENDER_THREADS")) {
    options.SetIntraOpNumThreads(hardware_worker_threads());
  }
  options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
  // Without the arena, activation memory goes back to the system after each run instead
  // of staying reserved for the life of the session.
  options.DisableCpuMemArena();
  return options;
}

// The file a session loads: HighQuality's files may be in the downloaded models folder.
std::filesystem::path session_path(const char* file) {
  const std::string_view name(file);
  if (name == kEfficientSamEncoderFile || name == kEfficientSamDecoderFile || name.starts_with("trustmark_")) {
    return high_quality_model_path(file);
  }
  return model_path(file);
}

std::shared_ptr<Ort::Session> open_session(Ort::Env& env, const char* file) {
  const auto path = session_path(file);
  try {
    return std::make_shared<Ort::Session>(env, path.c_str(), session_options());
  } catch (const Ort::Exception& error) {
    if (is_out_of_memory_message(error.what())) {
      throw InferenceOutOfMemory();
    }
    throw std::runtime_error(std::string("automatic selection could not load ") + file + ": " + error.what());
  } catch (const std::bad_alloc&) {
    throw InferenceOutOfMemory();
  }
}

std::shared_ptr<Ort::Session> shared_session(OrtState& state, std::shared_ptr<Ort::Session>& slot, const char* file) {
  if (!slot) {
    slot = open_session(environment(state), file);
  }
  return slot;
}

// Runs a session with cancellation wired to `cancel`; translates ONNX Runtime failures.
std::vector<Ort::Value> run(Ort::Session& session, const char* const* input_names, const Ort::Value* inputs,
                            std::size_t input_count, const char* const* output_names, std::size_t output_count,
                            InferenceCancel* cancel) {
  Ort::RunOptions run_options;
  if (cancel != nullptr) {
    if (cancel->requested()) {
      throw InferenceCancelled();
    }
    cancel->attach([&run_options] { run_options.SetTerminate(); });
  }
  try {
    auto outputs = session.Run(run_options, input_names, inputs, input_count, output_names, output_count);
    if (cancel != nullptr) {
      cancel->detach();
    }
    return outputs;
  } catch (const Ort::Exception& error) {
    if (cancel != nullptr) {
      cancel->detach();
      if (cancel->requested()) {
        throw InferenceCancelled();
      }
    }
    if (is_out_of_memory_message(error.what())) {
      throw InferenceOutOfMemory();
    }
    throw std::runtime_error(std::string("automatic selection failed: ") + error.what());
  } catch (const std::bad_alloc&) {
    if (cancel != nullptr) {
      cancel->detach();
    }
    throw InferenceOutOfMemory();
  }
}

Ort::MemoryInfo cpu_memory() {
  return Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
}

// MobileSAM's decoder: mask_input stays zero with has_mask_input 0 on every call, so a
// previous mask is never fed back (patent boundary in segmentation_engine.hpp). It upsamples
// its masks to the scaled frame itself.
AiMask decode_standard(Ort::Session& session, const SamEmbedding& embedding, const SamDecoderPrompt& prompt) {
  const auto& transform = embedding.transform;
  auto memory = cpu_memory();
  std::vector<float> embedding_values = embedding.values;
  auto coords = prompt.coords;
  auto labels = prompt.labels;
  std::vector<float> no_mask(static_cast<std::size_t>(kDecoderLowResSize * kDecoderLowResSize), 0.0F);
  float has_mask = 0.0F;
  std::array<float, 2> output_size{static_cast<float>(transform.scaled_height),
                                   static_cast<float>(transform.scaled_width)};
  const std::array<std::int64_t, 4> embedding_shape{1, 256, 64, 64};
  const std::array<std::int64_t, 3> coords_shape{1, 2, 2};
  const std::array<std::int64_t, 2> labels_shape{1, 2};
  const std::array<std::int64_t, 4> mask_shape{1, 1, kDecoderLowResSize, kDecoderLowResSize};
  const std::array<std::int64_t, 1> flag_shape{1};
  const std::array<std::int64_t, 1> size_shape{2};
  std::array<Ort::Value, 6> inputs{
      Ort::Value::CreateTensor<float>(memory, embedding_values.data(), embedding_values.size(),
                                      embedding_shape.data(), embedding_shape.size()),
      Ort::Value::CreateTensor<float>(memory, coords.data(), coords.size(), coords_shape.data(), coords_shape.size()),
      Ort::Value::CreateTensor<float>(memory, labels.data(), labels.size(), labels_shape.data(), labels_shape.size()),
      Ort::Value::CreateTensor<float>(memory, no_mask.data(), no_mask.size(), mask_shape.data(), mask_shape.size()),
      Ort::Value::CreateTensor<float>(memory, &has_mask, 1, flag_shape.data(), flag_shape.size()),
      Ort::Value::CreateTensor<float>(memory, output_size.data(), output_size.size(), size_shape.data(),
                                      size_shape.size()),
  };
  constexpr std::array<const char*, 6> input_names{"image_embeddings", "point_coords", "point_labels",
                                                   "mask_input",       "has_mask_input", "orig_im_size"};
  constexpr std::array<const char*, 2> output_names{"masks", "iou_predictions"};
  auto outputs = run(session, input_names.data(), inputs.data(), inputs.size(), output_names.data(),
                     output_names.size(), nullptr);

  const auto shape = outputs[0].GetTensorTypeAndShapeInfo().GetShape();
  if (shape.size() != 4 || shape[1] != kDecoderMaskCount || shape[2] != transform.scaled_height ||
      shape[3] != transform.scaled_width) {
    throw std::runtime_error("automatic selection: unexpected decoder output shape");
  }
  const auto chosen = best_sam_candidate(outputs[1].GetTensorData<float>(), prompt.first_candidate);
  const auto plane = static_cast<std::size_t>(transform.scaled_width) * static_cast<std::size_t>(transform.scaled_height);
  const float* logits = outputs[0].GetTensorData<float>() + static_cast<std::size_t>(chosen) * plane;
  return document_mask_from_logits(logits, transform.scaled_width, transform.scaled_height, transform.source_width,
                                   transform.source_height, LogitMapping::SamEdgeRamp);
}

// EfficientViT-SAM's decoder has no mask input at all, so a previous mask cannot be fed
// back. It returns its masks on the low-resolution grid.
AiMask decode_high_quality(Ort::Session& session, const SamEmbedding& embedding, const SamDecoderPrompt& prompt) {
  auto memory = cpu_memory();
  std::vector<float> embedding_values = embedding.values;
  auto coords = prompt.coords;
  auto labels = prompt.labels;
  const std::array<std::int64_t, 4> embedding_shape{1, 256, 64, 64};
  const std::array<std::int64_t, 3> coords_shape{1, 2, 2};
  const std::array<std::int64_t, 2> labels_shape{1, 2};
  std::array<Ort::Value, 3> inputs{
      Ort::Value::CreateTensor<float>(memory, embedding_values.data(), embedding_values.size(),
                                      embedding_shape.data(), embedding_shape.size()),
      Ort::Value::CreateTensor<float>(memory, coords.data(), coords.size(), coords_shape.data(), coords_shape.size()),
      Ort::Value::CreateTensor<float>(memory, labels.data(), labels.size(), labels_shape.data(), labels_shape.size()),
  };
  constexpr std::array<const char*, 3> input_names{"image_embeddings", "point_coords", "point_labels"};
  constexpr std::array<const char*, 2> output_names{"low_res_masks", "iou_predictions"};
  auto outputs = run(session, input_names.data(), inputs.data(), inputs.size(), output_names.data(),
                     output_names.size(), nullptr);

  const auto shape = outputs[0].GetTensorTypeAndShapeInfo().GetShape();
  if (shape.size() != 4 || shape[1] != kDecoderMaskCount || shape[2] != kSamLowResSize ||
      shape[3] != kSamLowResSize ||
      outputs[1].GetTensorTypeAndShapeInfo().GetElementCount() != static_cast<std::size_t>(kDecoderMaskCount)) {
    throw std::runtime_error("automatic selection: unexpected decoder output shape");
  }
  const auto chosen = best_sam_candidate(outputs[1].GetTensorData<float>(), prompt.first_candidate);
  const auto plane = static_cast<std::size_t>(kSamLowResSize) * static_cast<std::size_t>(kSamLowResSize);
  return document_mask_from_low_res_logits(
      outputs[0].GetTensorData<float>() + static_cast<std::size_t>(chosen) * plane, embedding.transform);
}

bool standard_model_present() {
  if (model_directory().empty()) {
    return false;
  }
  for (const auto* file : {kMobileSamEncoderFile, kMobileSamDecoderFile}) {
    std::error_code error;
    if (!std::filesystem::is_regular_file(model_path(file), error)) {
      return false;
    }
  }
  return true;
}

}  // namespace

// One decoder run from exactly one prompt (segmentation_engine_internal.hpp), with the
// decoder of the model that made the embedding.
AiMask decode_prompt(const SamEmbedding& embedding, const SamDecoderPrompt& prompt) {
  if (embedding.values.size() != kSamEmbeddingValues) {
    throw std::invalid_argument("automatic selection: malformed image embedding");
  }
  const bool high_quality = embedding.model == SamModel::HighQuality;
  auto& state = ort_state();
  std::shared_ptr<Ort::Session> session;
  {
    const std::lock_guard lock(state.mutex);
    session = high_quality ? shared_session(state, state.high_quality_decoder, kEfficientSamDecoderFile)
                           : shared_session(state, state.standard_decoder, kMobileSamDecoderFile);
  }
  return high_quality ? decode_high_quality(*session, embedding, prompt) : decode_standard(*session, embedding, prompt);
}

bool segmentation_available() {
  return segmentation_unavailable_reason().empty();
}

std::string segmentation_unavailable_reason() {
  if (!runtime().loaded) {
    return runtime().error;
  }
  if (model_directory().empty()) {
    return "the automatic selection models folder is not set";
  }
  if (!standard_model_present() && !high_quality_model_available()) {
    return std::string("the automatic selection model ") + kMobileSamEncoderFile + " is missing";
  }
  return {};
}

bool subject_selection_available() {
  // PATCHY_SELECT_SUBJECT comes from CMake's PATCHY_ENABLE_SELECT_SUBJECT. Checked here as
  // well as in the staging step, so a model file left in a build tree never re-enables it.
  if (PATCHY_SELECT_SUBJECT == 0 || !segmentation_available()) {
    return false;
  }
  std::error_code error;
  return std::filesystem::is_regular_file(model_path(kSubjectModelFile), error);
}

std::shared_ptr<SamEmbedding> encode_frame(const PixelBuffer& rgba8, InferenceCancel* cancel) {
  // The user's model choice (active_sam_model), never anything about the image.
  const auto model = active_sam_model() == SamModel::HighQuality || !standard_model_present()
                         ? SamModel::HighQuality
                         : SamModel::Standard;
  const bool high_quality = model == SamModel::HighQuality;
  auto& state = ort_state();
  std::shared_ptr<Ort::Session> session;
  {
    const std::lock_guard lock(state.mutex);
    session = high_quality ? shared_session(state, state.high_quality_encoder, kEfficientSamEncoderFile)
                           : shared_session(state, state.standard_encoder, kMobileSamEncoderFile);
  }
  auto embedding = std::make_shared<SamEmbedding>();
  embedding->model = model;
  embedding->transform = sam_transform(rgba8.width(), rgba8.height());
  std::vector<float> input;
  std::vector<std::int64_t> shape;
  if (high_quality) {
    // NCHW, normalized and padded to the square frame.
    input = padded_sam_encoder_input(rgba8, embedding->transform);
    shape = {1, 3, kSamInputSize, kSamInputSize};
  } else {
    // HWC in 0..255 at the scaled size; the MobileSAM export normalizes and pads itself.
    input = sam_encoder_input(rgba8, embedding->transform);
    shape = {embedding->transform.scaled_height, embedding->transform.scaled_width, 3};
  }
  auto memory = cpu_memory();
  auto tensor = Ort::Value::CreateTensor<float>(memory, input.data(), input.size(), shape.data(), shape.size());
  constexpr std::array<const char*, 1> input_names{"input_image"};
  constexpr std::array<const char*, 1> output_names{"image_embeddings"};
  auto outputs = run(*session, input_names.data(), &tensor, 1, output_names.data(), 1, cancel);
  const float* values = outputs[0].GetTensorData<float>();
  if (outputs[0].GetTensorTypeAndShapeInfo().GetElementCount() != kSamEmbeddingValues) {
    throw std::runtime_error("automatic selection: unexpected encoder output shape");
  }
  embedding->values.assign(values, values + kSamEmbeddingValues);
  return embedding;
}

AiMask subject_matte(const PixelBuffer& rgba8, InferenceCancel* cancel) {
  if (!subject_selection_available()) {
    throw std::runtime_error("this build of Patchy does not include Select Subject");
  }
  std::shared_ptr<Ort::Session> session;
  {
    auto& state = ort_state();
    const std::lock_guard lock(state.mutex);
    session = shared_session(state, state.subject, kSubjectModelFile);
  }
  if (cancel != nullptr && cancel->requested()) {
    throw InferenceCancelled();
  }
  auto input = subject_model_input(rgba8);
  const std::array<std::int64_t, 4> shape{1, 3, kSubjectInputSize, kSubjectInputSize};
  auto memory = cpu_memory();
  auto tensor = Ort::Value::CreateTensor<float>(memory, input.data(), input.size(), shape.data(), shape.size());
  constexpr std::array<const char*, 1> input_names{"input_image"};
  constexpr std::array<const char*, 1> output_names{"output_image"};
  auto outputs = run(*session, input_names.data(), &tensor, 1, output_names.data(), 1, cancel);
  if (outputs[0].GetTensorTypeAndShapeInfo().GetElementCount() !=
      static_cast<std::size_t>(kSubjectInputSize) * static_cast<std::size_t>(kSubjectInputSize)) {
    throw std::runtime_error("automatic selection: unexpected subject model output shape");
  }
  // One pass over the whole image, upsampled once: no crop or second pass (patent boundary
  // in segmentation_engine.hpp).
  return document_mask_from_logits(outputs[0].GetTensorData<float>(), kSubjectInputSize, kSubjectInputSize,
                                   rgba8.width(), rgba8.height(), LogitMapping::Sigmoid);
}

int model_download_permille() noexcept {
  return -1;
}

std::string trustmark_unavailable_reason() {
  if (!runtime().loaded) {
    return runtime().error;
  }
  for (const auto name : trustmark_model_files()) {
    std::error_code error;
    if (!std::filesystem::is_regular_file(high_quality_model_path(std::string(name).c_str()), error)) {
      return "the watermark models are not downloaded";
    }
  }
  return {};
}

bool trustmark_models_available() {
  return trustmark_unavailable_reason().empty();
}

bool trustmark_runtime_available() {
  return runtime().loaded;
}

namespace {

std::shared_ptr<Ort::Session> trustmark_session(std::size_t index) {
  const std::string file(trustmark_model_files().at(index));
  auto& state = ort_state();
  const std::lock_guard lock(state.mutex);
  return shared_session(state, state.trustmark[index], file.c_str());
}

}  // namespace

std::array<float, kTrustMarkPacketBits> trustmark_decode_logits(TrustMarkVariant variant,
                                                                const std::vector<float>& image,
                                                                InferenceCancel* cancel) {
  const std::int64_t size = trustmark_decoder_input_size(variant);
  if (image.size() != static_cast<std::size_t>(3 * size * size)) {
    throw std::invalid_argument("TrustMark: malformed decoder input");
  }
  const auto index = variant == TrustMarkVariant::Q ? 0U : variant == TrustMarkVariant::P ? 1U : 2U;
  const auto session = trustmark_session(index);
  auto values = image;
  const std::array<std::int64_t, 4> shape{1, 3, size, size};
  auto memory = cpu_memory();
  auto tensor = Ort::Value::CreateTensor<float>(memory, values.data(), values.size(), shape.data(), shape.size());
  constexpr std::array<const char*, 1> input_names{"image"};
  constexpr std::array<const char*, 1> output_names{"output"};
  auto outputs = run(*session, input_names.data(), &tensor, 1, output_names.data(), 1, cancel);
  if (outputs[0].GetTensorTypeAndShapeInfo().GetElementCount() != static_cast<std::size_t>(kTrustMarkPacketBits)) {
    throw std::runtime_error("TrustMark: unexpected decoder output shape");
  }
  std::array<float, kTrustMarkPacketBits> logits{};
  const float* data = outputs[0].GetTensorData<float>();
  std::copy(data, data + kTrustMarkPacketBits, logits.begin());
  return logits;
}

std::vector<float> trustmark_encode_q(const std::vector<float>& cover,
                                      const std::array<float, kTrustMarkPacketBits>& packet,
                                      InferenceCancel* cancel) {
  constexpr std::int64_t size = kTrustMarkEncoderInputSize;
  if (cover.size() != static_cast<std::size_t>(3 * size * size)) {
    throw std::invalid_argument("TrustMark: malformed encoder input");
  }
  const auto session = trustmark_session(3U);
  auto image = cover;
  auto bits = packet;
  const std::array<std::int64_t, 4> image_shape{1, 3, size, size};
  const std::array<std::int64_t, 2> bits_shape{1, kTrustMarkPacketBits};
  auto memory = cpu_memory();
  std::array<Ort::Value, 2> inputs{
      Ort::Value::CreateTensor<float>(memory, image.data(), image.size(), image_shape.data(), image_shape.size()),
      Ort::Value::CreateTensor<float>(memory, bits.data(), bits.size(), bits_shape.data(), bits_shape.size()),
  };
  constexpr std::array<const char*, 2> input_names{"onnx::Concat_0", "onnx::Gemm_1"};
  constexpr std::array<const char*, 1> output_names{"image"};
  auto outputs = run(*session, input_names.data(), inputs.data(), inputs.size(), output_names.data(), 1, cancel);
  if (outputs[0].GetTensorTypeAndShapeInfo().GetElementCount() != cover.size()) {
    throw std::runtime_error("TrustMark: unexpected encoder output shape");
  }
  const float* data = outputs[0].GetTensorData<float>();
  return {data, data + cover.size()};
}

void release_trustmark_models() {
  auto& state = ort_state();
  const std::lock_guard lock(state.mutex);
  for (auto& session : state.trustmark) {
    session.reset();
  }
}

void release_models() {
  auto& state = ort_state();
  const std::lock_guard lock(state.mutex);
  state.standard_encoder.reset();
  state.standard_decoder.reset();
  state.high_quality_encoder.reset();
  state.high_quality_decoder.reset();
  state.subject.reset();
  for (auto& session : state.trustmark) {
    session.reset();
  }
}

void release_subject_model() {
  auto& state = ort_state();
  const std::lock_guard lock(state.mutex);
  state.subject.reset();
}

void release_high_quality_model() {
  auto& state = ort_state();
  const std::lock_guard lock(state.mutex);
  state.high_quality_encoder.reset();
  state.high_quality_decoder.reset();
}

}  // namespace patchy::ai
