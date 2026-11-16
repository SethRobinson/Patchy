#include "ocr/ocr_fake_engine.hpp"

#include "core/environment.hpp"
#include "ocr/ocr_bands.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <numbers>
#include <thread>
#include <utility>

namespace patchy::ocr {

namespace {

std::vector<std::string_view> split(std::string_view text, char separator) {
  std::vector<std::string_view> parts;
  std::size_t start = 0;
  while (start <= text.size()) {
    const std::size_t end = std::min(text.find(separator, start), text.size());
    parts.push_back(text.substr(start, end - start));
    start = end + 1;
  }
  return parts;
}

std::optional<double> parse_number(std::string_view text) {
  // std::from_chars for doubles is missing from some of the supported standard libraries;
  // strtod reads "C" numbers here because nothing in a Qt-free library sets the C locale.
  const std::string copy(text);
  char* end = nullptr;
  const double value = std::strtod(copy.c_str(), &end);
  if (copy.empty() || end != copy.c_str() + copy.size()) {
    return std::nullopt;
  }
  return value;
}

void append_utf8(std::string& out, std::uint32_t code_point) {
  if (code_point < 0x80U) {
    out += static_cast<char>(code_point);
  } else if (code_point < 0x800U) {
    out += static_cast<char>(0xC0U | (code_point >> 6));
    out += static_cast<char>(0x80U | (code_point & 0x3FU));
  } else {
    out += static_cast<char>(0xE0U | (code_point >> 12));
    out += static_cast<char>(0x80U | ((code_point >> 6) & 0x3FU));
    out += static_cast<char>(0x80U | (code_point & 0x3FU));
  }
}

// "\uXXXX" becomes that character (BMP only: enough for kana, kanji and Cyrillic in an
// environment variable that may only carry ASCII safely).
std::string unescape(std::string_view text) {
  std::string out;
  for (std::size_t index = 0; index < text.size(); ++index) {
    if (text[index] == '\\' && index + 5 < text.size() && text[index + 1] == 'u') {
      std::uint32_t code_point = 0;
      const char* first = text.data() + index + 2;
      const auto parsed = std::from_chars(first, first + 4, code_point, 16);
      if (parsed.ec == std::errc() && parsed.ptr == first + 4) {
        append_utf8(out, code_point);
        index += 5;
        continue;
      }
    }
    out += text[index];
  }
  return out;
}

// The scripted words of a line: its text split at spaces, each given the share of the
// line's box that its characters take (bytes are a fine measure for a fake).
std::vector<Word> words_of(const std::string& text, const Box& box, bool vertical) {
  std::vector<Word> words;
  const double total = static_cast<double>(std::max<std::size_t>(1, text.size()));
  std::size_t start = 0;
  while (start < text.size()) {
    const std::size_t end = std::min(text.find(' ', start), text.size());
    if (end > start) {
      const double from = static_cast<double>(start) / total;
      const double to = static_cast<double>(end) / total;
      Word word;
      word.text = text.substr(start, end - start);
      word.box = vertical ? Box{box.x, box.y + box.height * from, box.width, box.height * (to - from)}
                          : Box{box.x + box.width * from, box.y, box.width * (to - from), box.height};
      word.quad = quad_from_box(word.box);
      words.push_back(std::move(word));
    }
    start = end + 1;
  }
  return words;
}

}  // namespace

class FakeRecognizer final : public Recognizer {
public:
  FakeRecognizer(const FakeEngine& engine, std::string language) : engine_(engine), language_(std::move(language)) {}

  Result recognize(const ImageView& image, const Cancel* cancel) override {
    FakeEngineConfig config;
    {
      const std::lock_guard lock(engine_.mutex_);
      config = engine_.config_;
    }
    if (!config.fail_with.empty()) {
      throw std::runtime_error(config.fail_with);
    }
    FakeEngine::Call call;
    call.language = language_;
    call.image_width = image.width;
    call.image_height = image.height;
    call.layout = image.layout;
    const auto row_samples = static_cast<std::size_t>(image.width) * static_cast<std::size_t>(channel_count(image.layout));
    for (std::int32_t y = 0; y < image.height && call.only_black_and_white; ++y) {
      const auto* row = image.data + static_cast<std::ptrdiff_t>(y) * image.stride;
      for (std::size_t sample = 0; sample < row_samples; ++sample) {
        if (row[sample] != 0U && row[sample] != 255U) {
          call.only_black_and_white = false;
          break;
        }
      }
    }
    const auto recognize_tile = [&](const Tile& tile) {
      call.tiles.emplace_back(tile.image.width, tile.image.height);
      // The wait is sliced so a cancel request answers within a few milliseconds.
      for (int waited = 0; waited < config.delay_ms; waited += 5) {
        if (cancel != nullptr && cancel->requested()) {
          throw RecognitionCancelled();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(std::min(5, config.delay_ms - waited)));
      }
      const double scale_x = static_cast<double>(tile.scaled_width) / tile.source_width;
      const double scale_y = static_cast<double>(tile.scaled_height) / tile.source_height;
      Result found;
      found.text_angle_degrees = config.text_angle_degrees;
      for (const auto& scripted : config.lines) {
        // The line as this tile sees it; a real engine reads what lies inside its pixels,
        // so a line whose centre is outside the tile is not reported from here.
        const Box in_tile{scripted.box.x * scale_x, scripted.box.y * scale_y - tile.band_start,
                          scripted.box.width * scale_x, scripted.box.height * scale_y};
        const double center_y = in_tile.y + in_tile.height / 2.0;
        const double center_x = in_tile.x + in_tile.width / 2.0;
        if (center_y < 0.0 || center_y >= tile.image.height || center_x < 0.0 || center_x >= tile.image.width) {
          continue;
        }
        Line line;
        line.text = scripted.text;
        line.quad = quad_from_box(in_tile);
        line.vertical = scripted.vertical;
        line.words = words_of(scripted.text, in_tile, scripted.vertical);
        if (scripted.angle_degrees != 0.0) {
          const double radians = scripted.angle_degrees * std::numbers::pi / 180.0;
          const auto turned = [&](Point point) {
            const double dx = point.x - center_x;
            const double dy = point.y - center_y;
            return Point{center_x + dx * std::cos(radians) - dy * std::sin(radians),
                         center_y + dx * std::sin(radians) + dy * std::cos(radians)};
          };
          line.angle_degrees = scripted.angle_degrees;
          for (auto& corner : line.quad) {
            corner = turned(corner);
          }
          for (auto& word : line.words) {
            for (auto& corner : word.quad) {
              corner = turned(corner);
            }
            word.box = box_from_quad(word.quad);
          }
        }
        line.box = box_from_quad(line.quad);
        found.lines.push_back(std::move(line));
      }
      return found;
    };
    Result result = recognize_in_bands(image, config.max_dimension, recognize_tile, cancel);
    {
      const std::lock_guard lock(engine_.mutex_);
      engine_.calls_.push_back(std::move(call));
    }
    return result;
  }

private:
  const FakeEngine& engine_;
  std::string language_;
};

FakeEngine::FakeEngine(FakeEngineConfig config) : config_(std::move(config)) {}

std::string_view FakeEngine::id() const {
  return config_.id;
}

std::string FakeEngine::display_name() const {
  return config_.display_name;
}

std::vector<Language> FakeEngine::languages() const {
  return config_.languages;
}

std::string FakeEngine::default_language() const {
  return config_.languages.empty() ? std::string() : config_.languages.front().tag;
}

Unavailable FakeEngine::unavailable() const {
  if (config_.unavailable != Unavailable::None) {
    return config_.unavailable;
  }
  return config_.languages.empty() ? Unavailable::NoLanguages : Unavailable::None;
}

std::string FakeEngine::unavailable_reason() const {
  if (unavailable() == Unavailable::None) {
    return {};
  }
  return config_.unavailable_reason.empty() ? std::string("the scripted test engine has no language")
                                            : config_.unavailable_reason;
}

std::unique_ptr<Recognizer> FakeEngine::open(std::string_view language_tag) const {
  if (unavailable() != Unavailable::None) {
    throw std::runtime_error(unavailable_reason());
  }
  const bool known = std::any_of(config_.languages.begin(), config_.languages.end(),
                                 [&](const Language& language) { return language.tag == language_tag; });
  if (!known) {
    throw std::runtime_error("the scripted test engine has no language \"" + std::string(language_tag) + "\"");
  }
  {
    const std::lock_guard lock(mutex_);
    ++open_count_;
  }
  return std::make_unique<FakeRecognizer>(*this, std::string(language_tag));
}

std::vector<FakeEngine::Call> FakeEngine::calls() const {
  const std::lock_guard lock(mutex_);
  return calls_;
}

int FakeEngine::open_count() const {
  const std::lock_guard lock(mutex_);
  return open_count_;
}

void FakeEngine::set_lines(std::vector<FakeLine> lines) {
  const std::lock_guard lock(mutex_);
  config_.lines = std::move(lines);
}

std::shared_ptr<FakeEngine> make_fake_engine(FakeEngineConfig config) {
  return std::make_shared<FakeEngine>(std::move(config));
}

std::vector<FakeLine> parse_fake_lines(std::string_view spec) {
  std::vector<FakeLine> lines;
  for (std::string_view entry : split(spec, ';')) {
    FakeLine line;
    if (entry.starts_with("v:")) {
      line.vertical = true;
      entry.remove_prefix(2);
    }
    const std::size_t equals = entry.find('=');
    if (equals == std::string_view::npos) {
      continue;
    }
    const auto numbers = split(entry.substr(0, equals), ',');
    if (numbers.size() != 4) {
      continue;
    }
    const auto x = parse_number(numbers[0]);
    const auto y = parse_number(numbers[1]);
    const auto width = parse_number(numbers[2]);
    const auto height = parse_number(numbers[3]);
    if (!x || !y || !width || !height || !(*width > 0.0) || !(*height > 0.0)) {
      continue;
    }
    line.box = Box{*x, *y, *width, *height};
    line.text = unescape(entry.substr(equals + 1));
    lines.push_back(std::move(line));
  }
  return lines;
}

std::vector<Language> parse_fake_languages(std::string_view spec) {
  std::vector<Language> languages;
  if (spec == "none") {
    return languages;
  }
  for (const std::string_view entry : split(spec, ';')) {
    const std::size_t equals = entry.find('=');
    if (equals == std::string_view::npos || equals == 0) {
      continue;
    }
    languages.push_back(Language{std::string(entry.substr(0, equals)), unescape(entry.substr(equals + 1))});
  }
  return languages;
}

std::optional<FakeEngineConfig> fake_engine_config_from_environment() {
  const auto spec = environment_variable("PATCHY_FAKE_OCR");
  if (!spec.has_value()) {
    return std::nullopt;
  }
  FakeEngineConfig config;
  config.lines = parse_fake_lines(*spec);
  if (const auto languages = environment_variable("PATCHY_FAKE_OCR_LANGUAGES")) {
    config.languages = parse_fake_languages(*languages);
  }
  if (const auto limit = environment_variable("PATCHY_FAKE_OCR_MAX_DIMENSION")) {
    config.max_dimension = static_cast<std::int32_t>(parse_number(*limit).value_or(0.0));
  }
  if (const auto delay = environment_variable("PATCHY_FAKE_OCR_DELAY_MS")) {
    config.delay_ms = static_cast<int>(parse_number(*delay).value_or(0.0));
  }
  if (const auto angle = environment_variable("PATCHY_FAKE_OCR_ANGLE")) {
    config.text_angle_degrees = parse_number(*angle);
  }
  return config;
}

}  // namespace patchy::ocr
