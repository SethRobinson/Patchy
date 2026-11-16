// The Windows text recognition engine (Windows.Media.Ocr, Windows 10 and 11), through
// C++/WinRT. This is the only translation unit that includes WinRT headers (they come
// from the Windows SDK's cppwinrt folder): no windows.h, no Qt, and no WinRT type leaves
// this file (docs/ocr.md, docs/platform-sites.md).
//
// Threading: every WinRT call here runs on a thread this file owns, in the multithreaded
// apartment. The Qt GUI thread is a single-threaded apartment, where a blocking wait on an
// asynchronous operation is not allowed, and pool threads may already sit in an apartment
// of another kind (the WIC decoders initialize some as single-threaded), where
// init_apartment would throw.

#define WINRT_LEAN_AND_MEAN

#include "ocr/ocr_platform.hpp"

#include "ocr/ocr_bands.hpp"
#include "support/translate_noop.hpp"

#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Globalization.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Media.Ocr.h>
#include <winrt/Windows.Storage.Streams.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <exception>
#include <functional>
#include <future>
#include <mutex>
#include <numbers>
#include <thread>
#include <utility>

namespace patchy::ocr::detail {

namespace {

namespace imaging = winrt::Windows::Graphics::Imaging;
namespace media = winrt::Windows::Media::Ocr;
using winrt::Windows::Foundation::AsyncStatus;

// The engine documents a smallest image side; tiles under this are padded with white.
constexpr std::int32_t kMinimumTileSide = 64;

// Runs `work` under the WinRT error translation: an hresult_error becomes a runtime_error
// carrying the system's message as UTF-8, so no WinRT type crosses a thread or this file.
template <typename Work>
auto translated(Work&& work) -> decltype(work()) {
  try {
    return work();
  } catch (const winrt::hresult_error& error) {
    throw std::runtime_error(winrt::to_string(error.message()));
  }
}

// Keeps the process's multithreaded apartment alive from the first use here until the
// process ends, with one parked thread that never leaves it. C++/WinRT caches each
// activation factory in static storage and uninit_apartment does not clear that cache: if
// the last thread left the apartment, COM would unload the recognizer's library under the
// cached factory, and the next call would jump into unloaded code (it did, on the second
// listing call, before this existed). The parked thread waits on nothing and owns nothing;
// process exit ends it.
void keep_apartment_alive() {
  static std::once_flag once;
  std::call_once(once, [] {
    std::promise<void> entered;
    auto waiting = entered.get_future();
    std::thread([entered = std::move(entered)]() mutable {
      try {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
      } catch (...) {
        // The callers' own init_apartment reports the failure.
      }
      entered.set_value();
      std::mutex mutex;
      std::condition_variable never;
      std::unique_lock lock(mutex);
      never.wait(lock, [] { return false; });
    }).detach();
    waiting.wait();
  });
}

// Calls `work` on a new thread in the multithreaded apartment and waits for it. For the
// short synchronous questions (which languages exist); a Recognizer keeps its own thread.
template <typename Work>
void on_apartment_thread(Work&& work) {
  keep_apartment_alive();
  std::exception_ptr failure;
  std::thread thread([&] {
    try {
      translated([&] {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        try {
          work();
        } catch (...) {
          winrt::uninit_apartment();
          throw;
        }
        winrt::uninit_apartment();
      });
    } catch (...) {
      failure = std::current_exception();
    }
  });
  thread.join();
  if (failure) {
    std::rethrow_exception(failure);
  }
}

// One thread in the multithreaded apartment that runs the jobs it is handed, one at a
// time, for as long as its owner lives.
class ApartmentThread {
public:
  ApartmentThread() : thread_([this] { run(); }) {}

  ~ApartmentThread() {
    {
      const std::lock_guard lock(mutex_);
      stopping_ = true;
    }
    wake_.notify_all();
    thread_.join();
  }

  ApartmentThread(const ApartmentThread&) = delete;
  ApartmentThread& operator=(const ApartmentThread&) = delete;

  // Runs `job` on the thread and waits; whatever it throws is rethrown here.
  void call(const std::function<void()>& job) {
    std::unique_lock lock(mutex_);
    job_ = &job;
    failure_ = nullptr;
    done_ = false;
    wake_.notify_all();
    finished_.wait(lock, [this] { return done_; });
    if (failure_) {
      std::rethrow_exception(std::exchange(failure_, nullptr));
    }
  }

private:
  void run() {
    keep_apartment_alive();
    bool apartment = false;
    std::exception_ptr startup_failure;
    try {
      translated([] { winrt::init_apartment(winrt::apartment_type::multi_threaded); });
      apartment = true;
    } catch (...) {
      startup_failure = std::current_exception();
    }
    std::unique_lock lock(mutex_);
    for (;;) {
      wake_.wait(lock, [this] { return stopping_ || job_ != nullptr; });
      if (job_ == nullptr) {
        break;  // stopping, nothing waiting
      }
      const std::function<void()>* job = std::exchange(job_, nullptr);
      lock.unlock();
      std::exception_ptr failure = startup_failure;
      if (!failure) {
        try {
          translated([&] { (*job)(); });
        } catch (...) {
          failure = std::current_exception();
        }
      }
      lock.lock();
      failure_ = failure;
      done_ = true;
      finished_.notify_all();
    }
    lock.unlock();
    if (apartment) {
      winrt::uninit_apartment();
    }
  }

  std::mutex mutex_;
  std::condition_variable wake_;
  std::condition_variable finished_;
  const std::function<void()>* job_{nullptr};
  std::exception_ptr failure_;
  bool done_{false};
  bool stopping_{false};
  std::thread thread_;  // last: it starts in the constructor and uses the members above
};

Point rotated_about(Point point, Point center, double degrees) noexcept {
  // Clockwise on screen (y down), the direction the engine reports its text angle in.
  const double radians = degrees * std::numbers::pi / 180.0;
  const double dx = point.x - center.x;
  const double dy = point.y - center.y;
  return Point{center.x + dx * std::cos(radians) - dy * std::sin(radians),
               center.y + dx * std::sin(radians) + dy * std::cos(radians)};
}

// One tile through the engine. `tile` is opaque BGRA. `join_words`: the language writes no
// spaces between words (Japanese, Chinese), where the engine's "words" are single
// characters and its line text puts a space between every two of them.
Result recognize_tile(const media::OcrEngine& engine, const Tile& tile, const Cancel* cancel, bool join_words) {
  const std::int32_t width = std::max(tile.image.width, kMinimumTileSide);
  const std::int32_t height = std::max(tile.image.height, kMinimumTileSide);
  const auto bytes = static_cast<std::uint32_t>(width) * static_cast<std::uint32_t>(height) * 4U;
  winrt::Windows::Storage::Streams::Buffer buffer(bytes);
  buffer.Length(bytes);
  std::uint8_t* pixels = buffer.data();
  if (width != tile.image.width || height != tile.image.height) {
    std::memset(pixels, 255, bytes);  // white padding to the right and below
  }
  for (std::int32_t row = 0; row < tile.image.height; ++row) {
    std::memcpy(pixels + static_cast<std::size_t>(row) * static_cast<std::size_t>(width) * 4U,
                tile.image.data + static_cast<std::ptrdiff_t>(row) * tile.image.stride,
                static_cast<std::size_t>(tile.image.width) * 4U);
  }
  const auto bitmap = imaging::SoftwareBitmap::CreateCopyFromBuffer(
      buffer, imaging::BitmapPixelFormat::Bgra8, width, height, imaging::BitmapAlphaMode::Premultiplied);

  const auto operation = engine.RecognizeAsync(bitmap);
  // One completion handler and a timed wait on it, so a cancel request answers within a few
  // milliseconds instead of after the whole tile. (Calling wait_for in a loop is not an
  // option: each call assigns the operation's one Completed delegate again, which the
  // system refuses the second time.) The state is shared because a cancelled wait returns
  // before the handler has run.
  struct Completion {
    std::mutex mutex;
    std::condition_variable changed;
    bool done{false};
  };
  const auto completion = std::make_shared<Completion>();
  operation.Completed([completion](const auto&, AsyncStatus) {
    {
      const std::lock_guard lock(completion->mutex);
      completion->done = true;
    }
    completion->changed.notify_all();
  });
  {
    std::unique_lock lock(completion->mutex);
    while (!completion->changed.wait_for(lock, std::chrono::milliseconds(20), [&] { return completion->done; })) {
      if (cancel != nullptr && cancel->requested()) {
        lock.unlock();
        operation.Cancel();
        throw RecognitionCancelled();
      }
    }
  }
  const media::OcrResult recognized = operation.GetResults();

  Result result;
  double angle = 0.0;
  if (const auto reported = recognized.TextAngle()) {
    angle = reported.Value();
    result.text_angle_degrees = angle;
  }
  // The engine reports word rectangles in the frame of the text turned upright about the
  // image centre; turning their corners back by the text angle puts them on the pixels.
  const Point center{width / 2.0, height / 2.0};
  for (const auto& recognized_line : recognized.Lines()) {
    Line line;
    line.text = winrt::to_string(recognized_line.Text());
    line.angle_degrees = angle;
    bool first = true;
    double left = 0.0;
    double top = 0.0;
    double right = 0.0;
    double bottom = 0.0;
    for (const auto& recognized_word : recognized_line.Words()) {
      const auto rect = recognized_word.BoundingRect();
      Word word;
      word.text = winrt::to_string(recognized_word.Text());
      const Box upright{rect.X, rect.Y, rect.Width, rect.Height};
      word.quad = quad_from_box(upright);
      for (auto& corner : word.quad) {
        corner = rotated_about(corner, center, angle);
      }
      word.box = box_from_quad(word.quad);
      left = first ? upright.x : std::min(left, upright.x);
      top = first ? upright.y : std::min(top, upright.y);
      right = first ? upright.x + upright.width : std::max(right, upright.x + upright.width);
      bottom = first ? upright.y + upright.height : std::max(bottom, upright.y + upright.height);
      first = false;
      line.words.push_back(std::move(word));
    }
    if (line.words.empty()) {
      continue;
    }
    if (join_words) {
      line.text.clear();
      for (const auto& word : line.words) {
        line.text += word.text;
      }
    }
    line.quad = quad_from_box(Box{left, top, right - left, bottom - top});
    for (auto& corner : line.quad) {
      corner = rotated_about(corner, center, angle);
    }
    line.box = box_from_quad(line.quad);
    // A column of characters: the words (single characters for Japanese and Chinese) run
    // down the page instead of across it. Read from the engine's own boxes.
    if (line.words.size() >= 2) {
      const Box& head = line.words.front().box;
      const Box& tail = line.words.back().box;
      const double across = std::abs((tail.x + tail.width / 2.0) - (head.x + head.width / 2.0));
      const double down = (tail.y + tail.height / 2.0) - (head.y + head.height / 2.0);
      line.vertical = down > 2.0 * across && (bottom - top) > 2.0 * (right - left);
    }
    result.lines.push_back(std::move(line));
  }
  return result;
}

class WindowsRecognizer final : public Recognizer {
public:
  explicit WindowsRecognizer(std::string language_tag) {
    const std::string_view primary = std::string_view(language_tag).substr(0, std::min<std::size_t>(language_tag.find('-'), language_tag.size()));
    join_words_ = primary == "ja" || primary == "zh";
    thread_.call([&] {
      const winrt::Windows::Globalization::Language language(winrt::to_hstring(language_tag));
      engine_ = media::OcrEngine::TryCreateFromLanguage(language);
      if (engine_ == nullptr) {
        throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "Windows has no text recognition for that language"));
      }
      max_dimension_ = static_cast<std::int32_t>(media::OcrEngine::MaxImageDimension());
    });
  }

  ~WindowsRecognizer() override {
    // The engine is released on the thread that made it, before that thread leaves its
    // apartment.
    try {
      thread_.call([this] { engine_ = nullptr; });
    } catch (...) {
    }
  }

  Result recognize(const ImageView& image, const Cancel* cancel) override {
    Result result;
    thread_.call([&] {
      result = recognize_in_bands(
          image, max_dimension_,
          [&](const Tile& tile) { return recognize_tile(engine_, tile, cancel, join_words_); }, cancel);
    });
    return result;
  }

private:
  media::OcrEngine engine_{nullptr};
  std::int32_t max_dimension_{0};
  bool join_words_{false};
  ApartmentThread thread_;  // last: destroyed first, after the destructor's release job
};

class WindowsEngine final : public Engine {
public:
  [[nodiscard]] std::string_view id() const override { return "windows"; }
  [[nodiscard]] std::string display_name() const override { return "Windows"; }

  [[nodiscard]] std::vector<Language> languages() const override {
    std::vector<Language> listed;
    try {
      on_apartment_thread([&] {
        for (const auto& language : media::OcrEngine::AvailableRecognizerLanguages()) {
          listed.push_back(
              Language{winrt::to_string(language.LanguageTag()), winrt::to_string(language.DisplayName())});
        }
      });
    } catch (const std::exception&) {
      listed.clear();  // unavailable() reports why
    }
    return listed;
  }

  [[nodiscard]] std::string default_language() const override {
    // The first of the user's Windows profile languages that has a recognizer: a system
    // setting, never anything about an image.
    std::string tag;
    try {
      on_apartment_thread([&] {
        if (const auto engine = media::OcrEngine::TryCreateFromUserProfileLanguages()) {
          tag = winrt::to_string(engine.RecognizerLanguage().LanguageTag());
        }
      });
    } catch (const std::exception&) {
    }
    if (tag.empty()) {
      const auto listed = languages();
      if (!listed.empty()) {
        tag = listed.front().tag;
      }
    }
    return tag;
  }

  [[nodiscard]] Unavailable unavailable() const override { return state().first; }
  [[nodiscard]] std::string unavailable_reason() const override { return state().second; }

  [[nodiscard]] std::unique_ptr<Recognizer> open(std::string_view language_tag) const override {
    return std::make_unique<WindowsRecognizer>(std::string(language_tag));
  }

private:
  [[nodiscard]] static std::pair<Unavailable, std::string> state() {
    bool any = false;
    try {
      on_apartment_thread([&] { any = media::OcrEngine::AvailableRecognizerLanguages().Size() > 0; });
    } catch (const std::exception& error) {
      return {Unavailable::Failed, error.what()};
    }
    if (!any) {
      return {Unavailable::NoLanguages,
              PATCHY_TRANSLATE_NOOP("QObject", "Windows has no text recognition language installed")};
    }
    return {Unavailable::None, std::string()};
  }
};

}  // namespace

std::vector<std::shared_ptr<Engine>> platform_engines() {
  return {std::make_shared<WindowsEngine>()};
}

}  // namespace patchy::ocr::detail
