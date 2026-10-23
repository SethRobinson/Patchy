// Automatic selection in the threaded wasm app. MobileSAM runs in the page's
// onnxruntime-web (scripts/wasm/ai-host-pre.js), in ORT's own worker and heap; patchy.wasm
// neither links ONNX Runtime nor hosts the models. docs/ai-selection.md ("WebAssembly")
// describes the transport. Read the patent boundary in segmentation_engine.hpp before
// changing any prompt or decoder input here or in the host.
//
// Every inference call runs on a pthread worker. It fills one slot of a static mailbox in
// the shared heap, bumps the mailbox sequence the host waits on, and then waits on the
// slot's state word. The host copies the inputs out, runs the model, writes the outputs
// back and notifies. The host never calls into wasm (docs/wasm.md: a raw JS-to-wasm call
// can clobber the Asyncify state of a suspended main loop), and the main thread never
// waits here: the host runs on it, so a main-thread call would wait forever.

#include "ai/segmentation_engine.hpp"
#include "ai/segmentation_engine_internal.hpp"
#include "ai/watermark_engine.hpp"

#include <emscripten/emscripten.h>
#include <emscripten/threading.h>

#include <array>
#include <atomic>
#include <chrono>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

#ifndef __EMSCRIPTEN_PTHREADS__
#error "segmentation_engine_web.cpp needs the threaded wasm build (workers wait for the page)"
#endif

// Called on the main thread only: there the JS runs in the page, where the pre-js installed
// Module.patchyAiAttach (on a worker it would run in the worker's own scope).
EM_JS(int, patchy_js_ai_attach_host,
      (void* sequence, void* slots, int slot_count, int slot_words, const char* ort_folder, const char* encoder_file,
       const char* decoder_file),
      {
        var attach = Module['patchyAiAttach'];
        return typeof attach === 'function' ? attach(sequence, slots, slot_count, slot_words, UTF8ToString(ort_folder),
                                                     UTF8ToString(encoder_file), UTF8ToString(decoder_file))
                                            : 0;
      });

EM_JS(void, patchy_js_ai_release_models, (), {
  if (typeof Module['patchyAiRelease'] === 'function') {
    Module['patchyAiRelease']();
  }
});

namespace patchy::ai {
namespace {

// Slot states, shared with ai-host-pre.js. kPending and kRunning are left only by
// compare-exchange, so a cancel (worker: kPending/kRunning -> kAbandoned) and the host
// (kPending -> kClaimed, kRunning -> kWriting) never both win. The worker never touches the
// slot's buffers while the host is in kClaimed (copying inputs) or kWriting (writing
// outputs). A slot is reused only after kFree: the worker frees kDone/kFailed once it has
// read the result, and the host frees kAbandoned once it has let go of it, so the host never
// writes into a slot or an output buffer that a newer request owns.
enum SlotState : std::int32_t {
  kFree = 0,
  kPreparing = 1,
  kPending = 2,
  kClaimed = 3,
  kRunning = 4,
  kWriting = 5,
  kDone = 6,
  kFailed = 7,
  kAbandoned = 8,
};

enum RequestKind : std::int32_t {
  kEncode = 1,
  kDecode = 2,
};

constexpr std::size_t kErrorBytes = 256;

// The host addresses every field as a 32-bit word index; keep the word numbers below and
// the W table in ai-host-pre.js in sync.
struct WebSlot {
  std::atomic<std::int32_t> state;     // 0
  std::int32_t kind;                   // 1
  std::atomic<std::int32_t> progress;  // 2 model download permille, -1 none (host writes)
  std::uint32_t input;                 // 3 const float*
  std::int32_t input_count;            // 4
  std::int32_t height;                 // 5 encode: scaled image height; decode: orig_im_size
  std::int32_t width;                  // 6
  float coords[4];                     // 7..10 decode: the one prompt's coordinates
  float labels[2];                     // 11..12 decode: its labels
  std::uint32_t output;                // 13 float*
  std::int32_t output_capacity;        // 14 floats
  std::int32_t output_count;           // 15 floats written (host writes)
  float scores[4];                     // 16..19 decode: predicted IoUs (host writes)
  char error[kErrorBytes];             // 20..83 UTF-8, NUL-terminated (host writes)
};
static_assert(sizeof(std::atomic<std::int32_t>) == 4 && std::atomic<std::int32_t>::is_always_lock_free);
static_assert(offsetof(WebSlot, input) == 3 * 4);
static_assert(offsetof(WebSlot, coords) == 7 * 4);
static_assert(offsetof(WebSlot, output) == 13 * 4);
static_assert(offsetof(WebSlot, scores) == 16 * 4);
static_assert(offsetof(WebSlot, error) == 20 * 4);
static_assert(sizeof(WebSlot) == 84 * 4);

constexpr int kSlotCount = 8;

struct Mailbox {
  std::atomic<std::int32_t> sequence;  // bumped for every post and abandon; the host waits on it
  std::array<WebSlot, kSlotCount> slots;
};

Mailbox g_mailbox{};

// -1 not attached yet, 0 the page has no host, 1 attached.
std::atomic<int> g_host_state{-1};
std::atomic<int> g_download_permille{-1};

// A request the host has not claimed after this long is given up: the page is not
// answering (the host itself failed), and a stuck "Analyzing image" wait would be worse
// than an error the user can retry.
constexpr auto kClaimTimeout = std::chrono::seconds(60);
constexpr auto kSlotTimeout = std::chrono::seconds(60);

void* futex_address(std::atomic<std::int32_t>& word) {
  return static_cast<void*>(&word);
}

void wake_host() {
  g_mailbox.sequence.fetch_add(1, std::memory_order_acq_rel);
  emscripten_futex_wake(futex_address(g_mailbox.sequence), INT_MAX);
}

// Attaches the page-side host once, from the main thread.
bool attach_host() {
  const auto state = g_host_state.load(std::memory_order_acquire);
  if (state >= 0) {
    return state == 1;
  }
  if (!emscripten_is_main_runtime_thread()) {
    return false;
  }
  const int attached = patchy_js_ai_attach_host(&g_mailbox.sequence, g_mailbox.slots.data(), kSlotCount,
                                                static_cast<int>(sizeof(WebSlot) / 4), PATCHY_ONNXRUNTIME_WEB_FOLDER,
                                                kMobileSamEncoderFile, kMobileSamDecoderFile);
  g_host_state.store(attached == 1 ? 1 : 0, std::memory_order_release);
  return attached == 1;
}

void require_worker_thread() {
  if (emscripten_is_main_runtime_thread()) {
    throw std::logic_error("automatic selection must run off the main thread in the web build");
  }
  if (g_host_state.load(std::memory_order_acquire) != 1) {
    throw std::runtime_error(segmentation_unavailable_reason());
  }
}

WebSlot& acquire_slot() {
  const auto started = std::chrono::steady_clock::now();
  for (;;) {
    for (auto& slot : g_mailbox.slots) {
      auto expected = static_cast<std::int32_t>(kFree);
      if (slot.state.compare_exchange_strong(expected, kPreparing, std::memory_order_acq_rel)) {
        return slot;
      }
    }
    if (std::chrono::steady_clock::now() - started > kSlotTimeout) {
      throw std::runtime_error("automatic selection is busy; try again");
    }
    emscripten_thread_sleep(20.0);
  }
}

struct Request {
  RequestKind kind{kEncode};
  const float* input{nullptr};
  std::size_t input_count{0};
  std::int32_t height{0};
  std::int32_t width{0};
  SamDecoderPrompt prompt{};
  float* output{nullptr};
  std::size_t output_capacity{0};
};

struct Response {
  std::size_t output_count{0};
  std::array<float, 4> scores{};
};

[[noreturn]] void throw_host_error(const std::string& message) {
  if (is_out_of_memory_message(message)) {
    throw InferenceOutOfMemory();
  }
  throw std::runtime_error("automatic selection failed: " + message);
}

// Posts one request and waits for its result. Throws InferenceCancelled after a cancel.
Response run(const Request& request, InferenceCancel* cancel) {
  if (cancel != nullptr && cancel->requested()) {
    throw InferenceCancelled();
  }
  auto& slot = acquire_slot();
  slot.kind = request.kind;
  slot.progress.store(-1, std::memory_order_relaxed);
  slot.input = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(request.input));
  slot.input_count = static_cast<std::int32_t>(request.input_count);
  slot.height = request.height;
  slot.width = request.width;
  std::memcpy(slot.coords, request.prompt.coords.data(), sizeof(slot.coords));
  std::memcpy(slot.labels, request.prompt.labels.data(), sizeof(slot.labels));
  slot.output = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(request.output));
  slot.output_capacity = static_cast<std::int32_t>(request.output_capacity);
  slot.output_count = 0;
  slot.error[0] = '\0';
  slot.state.store(kPending, std::memory_order_release);
  wake_host();

  const auto posted = std::chrono::steady_clock::now();
  for (;;) {
    auto state = slot.state.load(std::memory_order_acquire);
    if (state == kDone || state == kFailed) {
      g_download_permille.store(-1, std::memory_order_relaxed);
      if (state == kFailed) {
        // Copy the message out before the slot is reused.
        const std::string message(slot.error, strnlen(slot.error, kErrorBytes));
        slot.state.store(kFree, std::memory_order_release);
        throw_host_error(message);
      }
      Response response;
      response.output_count = static_cast<std::size_t>(slot.output_count);
      std::memcpy(response.scores.data(), slot.scores, sizeof(slot.scores));
      slot.state.store(kFree, std::memory_order_release);
      return response;
    }
    const auto progress = slot.progress.load(std::memory_order_relaxed);
    if (progress >= 0) {
      g_download_permille.store(progress, std::memory_order_relaxed);
    }
    const bool cancelled = cancel != nullptr && cancel->requested();
    const bool unanswered = state == kPending && std::chrono::steady_clock::now() - posted > kClaimTimeout;
    if ((cancelled || unanswered) && (state == kPending || state == kRunning) &&
        slot.state.compare_exchange_strong(state, kAbandoned, std::memory_order_acq_rel)) {
      // The host frees the slot once it lets go; nothing here touches it again.
      wake_host();
      g_download_permille.store(-1, std::memory_order_relaxed);
      if (cancelled) {
        throw InferenceCancelled();
      }
      throw std::runtime_error("the page did not answer the automatic selection request");
    }
    // kClaimed or kWriting: the host is copying; it moves on within one JS task.
    emscripten_futex_wait(futex_address(slot.state), static_cast<std::uint32_t>(state), 50.0);
  }
}

}  // namespace

// One decoder run from exactly one prompt (segmentation_engine_internal.hpp). The host
// feeds mask_input zero with has_mask_input 0 on every call: a previous mask is never fed
// back (patent boundary in segmentation_engine.hpp).
AiMask decode_prompt(const SamEmbedding& embedding, const SamDecoderPrompt& prompt) {
  require_worker_thread();
  if (embedding.values.size() != kSamEmbeddingValues) {
    throw std::invalid_argument("automatic selection: malformed image embedding");
  }
  const auto& transform = embedding.transform;
  const auto plane = static_cast<std::size_t>(transform.scaled_width) * static_cast<std::size_t>(transform.scaled_height);
  std::vector<float> masks(plane * static_cast<std::size_t>(kDecoderMaskCount));
  Request request;
  request.kind = kDecode;
  request.input = embedding.values.data();
  request.input_count = embedding.values.size();
  request.height = transform.scaled_height;
  request.width = transform.scaled_width;
  request.prompt = prompt;
  request.output = masks.data();
  request.output_capacity = masks.size();
  const auto response = run(request, nullptr);
  if (response.output_count != masks.size()) {
    throw std::runtime_error("automatic selection: unexpected decoder output shape");
  }
  const auto chosen = best_sam_candidate(response.scores.data(), prompt.first_candidate);
  return document_mask_from_logits(masks.data() + static_cast<std::size_t>(chosen) * plane, transform.scaled_width,
                                   transform.scaled_height, transform.source_width, transform.source_height,
                                   LogitMapping::SamEdgeRamp);
}

bool segmentation_available() {
  return attach_host();
}

std::string segmentation_unavailable_reason() {
  switch (g_host_state.load(std::memory_order_acquire)) {
    case 1:
      return {};
    case 0:
      return "the page does not provide the automatic selection host";
    default:
      return "automatic selection has not been set up yet";
  }
}

bool subject_selection_available() {
  // Select Subject's model is not part of the web build (docs/ai-selection.md).
  return false;
}

std::shared_ptr<SamEmbedding> encode_frame(const PixelBuffer& rgba8, InferenceCancel* cancel) {
  require_worker_thread();
  auto embedding = std::make_shared<SamEmbedding>();
  embedding->transform = sam_transform(rgba8.width(), rgba8.height());
  const auto input = sam_encoder_input(rgba8, embedding->transform);
  embedding->values.resize(kSamEmbeddingValues);
  Request request;
  request.kind = kEncode;
  request.input = input.data();
  request.input_count = input.size();
  request.height = embedding->transform.scaled_height;
  request.width = embedding->transform.scaled_width;
  request.output = embedding->values.data();
  request.output_capacity = embedding->values.size();
  const auto response = run(request, cancel);
  if (response.output_count != kSamEmbeddingValues) {
    throw std::runtime_error("automatic selection: unexpected encoder output shape");
  }
  return embedding;
}

AiMask subject_matte(const PixelBuffer&, InferenceCancel*) {
  throw std::runtime_error("this build of Patchy does not include Select Subject");
}

int model_download_permille() noexcept {
  return g_download_permille.load(std::memory_order_relaxed);
}

void release_models() {
  if (emscripten_is_main_runtime_thread() && g_host_state.load(std::memory_order_acquire) == 1) {
    patchy_js_ai_release_models();
  }
}

void release_subject_model() {}

void release_high_quality_model() {}

// The web build does not offer the watermark models (docs/watermarks.md).
std::string trustmark_unavailable_reason() {
  return "the watermark models are not available in the web version";
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
