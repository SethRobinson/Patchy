#pragma once

// Text recognition engines (docs/ocr.md). An Engine is one way to read text from pixels:
// the operating system's own (Windows.Media.Ocr, ocr_engine_win.cpp), a downloaded model
// run through ONNX Runtime (registered from src/ai through add_engine_provider), or the
// scripted fake the tests use. Qt-free, like src/ai.
//
// Binding rules (the "Text recognition" entry of docs/legal-constraints-scanning.md):
// recognition runs only on an explicit user action; the engine and the language are the
// user's picks from settings, never chosen from the image; nothing here identifies a
// typeface; and results never feed page detection, cutting or straightening anywhere else.
//
// Threading: Engine's listing calls are cheap and may run on the GUI thread. A Recognizer
// serves one caller thread at a time and must never be called on the GUI thread: an engine
// may block for seconds, and the Windows engine waits on an asynchronous operation, which
// a single-threaded apartment does not allow.

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace patchy::ocr {

// Straight (not premultiplied) alpha where there is alpha; engines read the image over
// white. `stride` is bytes per row and may exceed width * channels.
enum class PixelLayout { Gray8, Rgb8, Rgba8, Bgra8 };

struct ImageView {
  const std::uint8_t* data{nullptr};
  std::int32_t width{0};
  std::int32_t height{0};
  std::ptrdiff_t stride{0};
  PixelLayout layout{PixelLayout::Rgba8};
};

[[nodiscard]] int channel_count(PixelLayout layout) noexcept;

struct Point {
  double x{0.0};
  double y{0.0};
};

struct Box {
  double x{0.0};
  double y{0.0};
  double width{0.0};
  double height{0.0};
};

// The corners of a piece of text in its own frame: top left, top right, bottom right,
// bottom left as a reader of that text sees them. For upright text this is the box.
using Quad = std::array<Point, 4>;

[[nodiscard]] Quad quad_from_box(const Box& box) noexcept;
[[nodiscard]] Box box_from_quad(const Quad& quad) noexcept;

// Everything is in the pixels of the image handed to recognize(), y down. `box` is the
// axis-aligned bound of `quad`.
struct Word {
  std::string text;  // UTF-8
  Box box;
  Quad quad;
};

struct Line {
  std::string text;  // UTF-8, words joined as the engine reports the line
  Box box;
  Quad quad;
  // Clockwise rotation of the line's own frame, y down: 0 for upright text.
  double angle_degrees{0.0};
  // Characters run top to bottom (vertical Japanese or Chinese).
  bool vertical{false};
  std::vector<Word> words;
};

struct Result {
  std::vector<Line> lines;
  // The page-wide text rotation when the engine reports one.
  std::optional<double> text_angle_degrees;

  // The lines joined with '\n', in the order given.
  [[nodiscard]] std::string text() const;
};

// A choice in an engine's Language list: a BCP-47 tag for an operating system engine, a
// script group token for a model engine. `tag` is persisted in settings (ocr/language).
struct Language {
  std::string tag;
  std::string display_name;  // the engine's own name for it (Windows gives its localized one)
};

enum class Unavailable {
  None,
  // The engine exists but has no recognizer language installed (Windows without an OCR
  // language pack).
  NoLanguages,
  // The engine's files are a download the user has not made yet.
  NotDownloaded,
  // The engine cannot run here (its runtime failed to load, or the system refused).
  Failed,
};

// Requested from any thread; recognize() checks it between its steps.
class Cancel {
public:
  void request() noexcept { requested_.store(true, std::memory_order_relaxed); }
  [[nodiscard]] bool requested() const noexcept { return requested_.load(std::memory_order_relaxed); }

private:
  std::atomic<bool> requested_{false};
};

// Thrown by recognize() when its Cancel was requested.
class RecognitionCancelled final : public std::runtime_error {
public:
  RecognitionCancelled();
};

// One engine opened for one language. Hold it for a batch of images (the pages of an
// import), then drop it.
class Recognizer {
public:
  virtual ~Recognizer() = default;
  // Throws RecognitionCancelled on cancel and std::runtime_error (an English diagnostic)
  // when the engine fails. An image with no text is an empty Result, never an error.
  [[nodiscard]] virtual Result recognize(const ImageView& image, const Cancel* cancel = nullptr) = 0;
};

class Engine {
public:
  virtual ~Engine() = default;
  // Permanent token persisted in settings (ocr/engine) and used by scripts: "windows",
  // "ppocr", "fake". Never renamed.
  [[nodiscard]] virtual std::string_view id() const = 0;
  // English fallback name; the UI shows its own translated name for the ids it knows.
  [[nodiscard]] virtual std::string display_name() const = 0;
  // Asked fresh on every call: a language installed while Patchy runs shows up.
  [[nodiscard]] virtual std::vector<Language> languages() const = 0;
  // The tag to preselect before the user has chosen one: from the user's system or app
  // language settings, never from an image. Empty when there is no language.
  [[nodiscard]] virtual std::string default_language() const = 0;
  [[nodiscard]] virtual Unavailable unavailable() const = 0;
  // English diagnostic for logs and script errors; empty when available.
  [[nodiscard]] virtual std::string unavailable_reason() const = 0;
  // Throws std::runtime_error when the engine is unavailable or does not have `language_tag`.
  [[nodiscard]] virtual std::unique_ptr<Recognizer> open(std::string_view language_tag) const = 0;
};

// Every engine this build and this machine can list, available or not, in display order:
// the operating system's first, then the providers'. With PATCHY_FAKE_OCR set (ocr_fake_engine.hpp)
// the list is the scripted fake alone, so script and visual-QA runs do not depend on the
// machine. Thread-safe.
[[nodiscard]] std::vector<std::shared_ptr<Engine>> engines();
[[nodiscard]] std::shared_ptr<Engine> find_engine(std::string_view id);

// Engines that live outside this library (the ONNX model engine in src/ai) join the list
// through a provider, called on every engines() call. Registered once at startup.
using EngineProvider = std::function<std::vector<std::shared_ptr<Engine>>()>;
void add_engine_provider(EngineProvider provider);

// Tests replace the whole list (an empty factory result means "no engine on this system");
// an empty function restores the real one.
void set_engines_for_testing(std::function<std::vector<std::shared_ptr<Engine>>()> factory);

}  // namespace patchy::ocr
