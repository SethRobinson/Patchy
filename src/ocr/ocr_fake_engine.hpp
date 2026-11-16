#pragma once

// A scripted engine for tests, script runs and visual QA (docs/ocr.md, "Testing"): it
// "recognizes" the lines it was told about, wherever they intersect the pixels it is
// handed, and records what it was handed. It goes through the same banding as a real
// engine with a size limit, so tests pin that path too.
//
// In-process tests build one with make_fake_engine and install it with
// set_engines_for_testing. A real build can be driven without one through the environment:
//   PATCHY_FAKE_OCR="x,y,w,h=text;x,y,w,h=text"   lines in image pixels; prefix an entry
//                                                  with "v:" for a vertical line; \uXXXX
//                                                  escapes in the text; an empty value
//                                                  lists the engine with no lines
//   PATCHY_FAKE_OCR_LANGUAGES="tag=Name;tag=Name"  or "none" for an engine with no language
//   PATCHY_FAKE_OCR_MAX_DIMENSION=2600             size limit, 0 = none
//   PATCHY_FAKE_OCR_DELAY_MS=50                    per band, so Cancel can be exercised
//   PATCHY_FAKE_OCR_ANGLE=1.5                      reported page text angle

#include "ocr/ocr_engine.hpp"

#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace patchy::ocr {

struct FakeLine {
  Box box;  // in the pixels of the image given to recognize()
  std::string text;
  bool vertical{false};
  // A turned line: `box` is the line upright, and it is reported turned this many degrees
  // clockwise about the box's centre (quad and angle), as the Windows engine reports one.
  double angle_degrees{0.0};
};

struct FakeEngineConfig {
  std::string id{"fake"};
  std::string display_name{"Scripted test engine"};
  std::vector<Language> languages{{"en-US", "English (United States)"}};
  std::vector<FakeLine> lines;
  std::int32_t max_dimension{0};
  int delay_ms{0};
  std::optional<double> text_angle_degrees;
  // Other than None, the engine lists itself as unavailable and open() throws `reason`.
  Unavailable unavailable{Unavailable::None};
  std::string unavailable_reason;
  // When set, recognize() throws this instead of answering (an engine failing mid-run).
  std::string fail_with;
};

class FakeEngine final : public Engine {
public:
  explicit FakeEngine(FakeEngineConfig config);

  [[nodiscard]] std::string_view id() const override;
  [[nodiscard]] std::string display_name() const override;
  [[nodiscard]] std::vector<Language> languages() const override;
  [[nodiscard]] std::string default_language() const override;
  [[nodiscard]] Unavailable unavailable() const override;
  [[nodiscard]] std::string unavailable_reason() const override;
  [[nodiscard]] std::unique_ptr<Recognizer> open(std::string_view language_tag) const override;

  // What the engine has been asked so far, for assertions. Thread-safe.
  struct Call {
    std::string language;
    std::int32_t image_width{0};
    std::int32_t image_height{0};
    // One entry per band the image was read in: the tile's pixel size.
    std::vector<std::pair<std::int32_t, std::int32_t>> tiles;
  };
  [[nodiscard]] std::vector<Call> calls() const;
  [[nodiscard]] int open_count() const;
  // Replaces the scripted lines (a test that reads several different images).
  void set_lines(std::vector<FakeLine> lines);

private:
  friend class FakeRecognizer;
  FakeEngineConfig config_;
  mutable std::mutex mutex_;
  mutable std::vector<Call> calls_;
  mutable int open_count_{0};
};

[[nodiscard]] std::shared_ptr<FakeEngine> make_fake_engine(FakeEngineConfig config);

// The configuration PATCHY_FAKE_OCR and its companions describe, or nothing when
// PATCHY_FAKE_OCR is not set.
[[nodiscard]] std::optional<FakeEngineConfig> fake_engine_config_from_environment();
// The parsers behind it, exposed for tests. Malformed entries are skipped.
[[nodiscard]] std::vector<FakeLine> parse_fake_lines(std::string_view spec);
[[nodiscard]] std::vector<Language> parse_fake_languages(std::string_view spec);

}  // namespace patchy::ocr
