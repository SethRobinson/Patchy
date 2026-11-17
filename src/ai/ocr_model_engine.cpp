#include "ai/ocr_model_engine.hpp"

#include "ai/ocr_models.hpp"
#include "ocr/ocr_text_decode.hpp"
#include "ocr/ocr_text_detect.hpp"
#include "support/translate_noop.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace patchy::ai {

namespace {

constexpr std::string_view kEngineId = "ppocr";

// The picks and their recognizers. Tags are persisted: never rename one. Russian uses the
// Cyrillic recognizer and Ukrainian and Belarusian the East Slavic one: that is how they
// measured (docs/ocr-benchmark.md).
const std::vector<OcrModelLanguage>& language_table() {
  static const std::vector<OcrModelLanguage> table{
      {"en", PATCHY_TRANSLATE_NOOP("QObject", "English"), kOcrMainRecognizerFile, "ppocr_v6_small_rec.yml"},
      {"ja", PATCHY_TRANSLATE_NOOP("QObject", "Japanese"), kOcrMainRecognizerFile, "ppocr_v6_small_rec.yml"},
      {"zh-Hans", PATCHY_TRANSLATE_NOOP("QObject", "Chinese (Simplified)"), kOcrMainRecognizerFile,
       "ppocr_v6_small_rec.yml"},
      {"zh-Hant", PATCHY_TRANSLATE_NOOP("QObject", "Chinese (Traditional)"), kOcrMainRecognizerFile,
       "ppocr_v6_small_rec.yml"},
      {"latin", PATCHY_TRANSLATE_NOOP("QObject", "Other Latin-script languages (French, German, Spanish...)"),
       kOcrMainRecognizerFile, "ppocr_v6_small_rec.yml"},
      {"ko", PATCHY_TRANSLATE_NOOP("QObject", "Korean"), "ppocr_korean_rec.onnx", "ppocr_korean_rec.yml"},
      {"ru", PATCHY_TRANSLATE_NOOP("QObject", "Russian"), "ppocr_cyrillic_rec.onnx", "ppocr_cyrillic_rec.yml"},
      {"uk", PATCHY_TRANSLATE_NOOP("QObject", "Ukrainian"), "ppocr_eslav_rec.onnx", "ppocr_eslav_rec.yml"},
      {"be", PATCHY_TRANSLATE_NOOP("QObject", "Belarusian"), "ppocr_eslav_rec.onnx", "ppocr_eslav_rec.yml"},
      {"cyrillic", PATCHY_TRANSLATE_NOOP("QObject", "Other Cyrillic-script languages"), "ppocr_cyrillic_rec.onnx",
       "ppocr_cyrillic_rec.yml"},
      {"el", PATCHY_TRANSLATE_NOOP("QObject", "Greek"), "ppocr_el_rec.onnx", "ppocr_el_rec.yml"},
      {"th", PATCHY_TRANSLATE_NOOP("QObject", "Thai"), "ppocr_th_rec.onnx", "ppocr_th_rec.yml"},
      {"hi", PATCHY_TRANSLATE_NOOP("QObject", "Hindi, Marathi, Nepali (Devanagari)"), "ppocr_devanagari_rec.onnx",
       "ppocr_devanagari_rec.yml"},
      {"ta", PATCHY_TRANSLATE_NOOP("QObject", "Tamil"), "ppocr_ta_rec.onnx", "ppocr_ta_rec.yml"},
      {"te", PATCHY_TRANSLATE_NOOP("QObject", "Telugu"), "ppocr_te_rec.onnx", "ppocr_te_rec.yml"},
  };
  return table;
}

const OcrModelLanguage* find_language(std::string_view tag) {
  for (const auto& language : language_table()) {
    if (language.tag == tag) {
      return &language;
    }
  }
  return nullptr;
}

bool file_present(std::string_view file) {
  const auto path = ocr_model_path(file);
  std::error_code error;
  return !path.empty() && std::filesystem::is_regular_file(path, error);
}

std::mutex& state_mutex() {
  static std::mutex mutex;
  return mutex;
}

std::string& default_language_tag() {
  static std::string tag = "en";
  return tag;
}

// A recognizer's character list, read once from its yml and kept: the main one has 18,708
// entries.
std::shared_ptr<const std::vector<std::string>> character_list(std::string_view dictionary) {
  static std::map<std::string, std::shared_ptr<const std::vector<std::string>>, std::less<>> cache;
  {
    const std::lock_guard lock(state_mutex());
    if (const auto found = cache.find(dictionary); found != cache.end()) {
      return found->second;
    }
  }
  std::ifstream stream(ocr_model_path(dictionary), std::ios::binary);
  if (!stream) {
    throw std::runtime_error("the text recognition model's character list " + std::string(dictionary) + " is missing");
  }
  std::ostringstream text;
  text << stream.rdbuf();
  auto characters = std::make_shared<const std::vector<std::string>>(ocr::character_list_from_yaml(text.str()));
  const std::lock_guard lock(state_mutex());
  cache.insert_or_assign(std::string(dictionary), characters);
  return characters;
}

void check(const ocr::Cancel* cancel) {
  if (cancel != nullptr && cancel->requested()) {
    throw ocr::RecognitionCancelled();
  }
}

ocr::Point centre(const ocr::Quad& quad) noexcept {
  return {(quad[0].x + quad[1].x + quad[2].x + quad[3].x) / 4.0, (quad[0].y + quad[1].y + quad[2].y + quad[3].y) / 4.0};
}

std::string trimmed(std::string text) {
  const auto first = text.find_first_not_of(' ');
  if (first == std::string::npos) {
    return {};
  }
  const auto last = text.find_last_not_of(' ');
  return text.substr(first, last - first + 1);
}

class ModelRecognizer final : public ocr::Recognizer {
public:
  explicit ModelRecognizer(const OcrModelLanguage& language)
      : language_(language), characters_(character_list(language.dictionary)) {}

  [[nodiscard]] ocr::Result recognize(const ocr::ImageView& image, const ocr::Cancel* cancel) override {
    check(cancel);
    ocr::Result result;
    // The lines: one detector run over the image scaled to its cap.
    std::vector<ocr::DetectedText> detected;
    {
      const auto input = ocr::detector_input(image);
      check(cancel);
      const auto output = run_ocr_model(kOcrDetectorFile, input.values, {1, 3, input.tensor_height, input.tensor_width});
      const auto plane = static_cast<std::size_t>(input.tensor_width) * static_cast<std::size_t>(input.tensor_height);
      if (output.values.size() != plane) {
        throw std::runtime_error("text recognition: the detector's output has an unexpected size");
      }
      detected = ocr::detected_text(output.values, input.tensor_width, input.tensor_height,
                                    static_cast<double>(image.width) / input.scaled_width,
                                    static_cast<double>(image.height) / input.scaled_height);
    }
    // Each line is cut from the image at its own size and read on its own: one line a run
    // costs no time against batches and half their memory (docs/ocr-benchmark.md).
    for (const auto& text : detected) {
      check(cancel);
      const auto input = ocr::line_input(image, text.quad);
      const auto output = run_ocr_model(language_.recognizer, input.values, {1, 3, ocr::kRecognizerHeight, input.tensor_width});
      if (output.shape.size() != 3 || output.shape[0] != 1) {
        throw std::runtime_error("text recognition: the recognizer's output has an unexpected shape");
      }
      const auto steps = static_cast<std::int32_t>(output.shape[1]);
      const auto decoded = ocr::decode_line(output.values, steps, static_cast<std::int32_t>(output.shape[2]), *characters_);
      ocr::Line line;
      line.text = trimmed(decoded.text);
      if (line.text.empty()) {
        continue;  // the detector saw something the recognizer reads as nothing
      }
      // The line is reported where its ink is (the map's region, a little grown), not as the
      // padded rectangle the recognizer read: the padding made short words a third too
      // wide and every line half again too thick (docs/ocr-engines.md, "Line boxes").
      line.quad = text.line;
      line.box = ocr::box_from_quad(text.line);
      line.angle_degrees = text.angle_degrees;
      line.vertical = input.vertical;
      if (input.vertical) {
        line.words.push_back({line.text, line.box, line.quad});
      } else {
        line.words = ocr::words_on_line(
            ocr::words_from_decoded_line(decoded, steps, static_cast<double>(input.text_width) / input.tensor_width,
                                         text.quad),
            text.line);
      }
      result.lines.push_back(std::move(line));
    }
    // Reading order from the boxes alone: level lines top to bottom (left first on a shared
    // row), then columns right to left (the higher first on a shared column).
    std::stable_sort(result.lines.begin(), result.lines.end(), [](const ocr::Line& a, const ocr::Line& b) {
      if (a.vertical != b.vertical) {
        return !a.vertical;
      }
      const auto ca = centre(a.quad);
      const auto cb = centre(b.quad);
      if (a.vertical) {
        return ca.x != cb.x ? ca.x > cb.x : ca.y < cb.y;
      }
      return ca.y != cb.y ? ca.y < cb.y : ca.x < cb.x;
    });
    // Two lines whose centres are less than half a line apart share a row: the left one first.
    for (std::size_t index = 0; index + 1 < result.lines.size(); ++index) {
      auto& a = result.lines[index];
      auto& b = result.lines[index + 1];
      if (a.vertical || b.vertical) {
        continue;
      }
      const auto ca = centre(a.quad);
      const auto cb = centre(b.quad);
      if (std::abs(ca.y - cb.y) < 0.5 * std::min(a.box.height, b.box.height) && cb.x < ca.x) {
        std::swap(a, b);
      }
    }
    return result;
  }

private:
  OcrModelLanguage language_;
  std::shared_ptr<const std::vector<std::string>> characters_;
};

class ModelEngine final : public ocr::Engine {
public:
  [[nodiscard]] std::string_view id() const override { return kEngineId; }
  [[nodiscard]] std::string display_name() const override { return "PP-OCR"; }

  [[nodiscard]] std::vector<ocr::Language> languages() const override {
    std::vector<ocr::Language> listed;
    for (const auto& language : language_table()) {
      listed.push_back({std::string(language.tag), std::string(language.display_name)});
    }
    return listed;
  }

  [[nodiscard]] std::string default_language() const override {
    const std::lock_guard lock(state_mutex());
    return default_language_tag();
  }

  [[nodiscard]] ocr::Unavailable unavailable() const override {
    if (!ocr_runtime_unavailable_reason().empty()) {
      return ocr::Unavailable::Failed;
    }
    // The detector and the main recognizer are the engine; every pick's own files are
    // checked when it is opened.
    for (const auto file : {kOcrDetectorFile, kOcrMainRecognizerFile, language_table().front().dictionary}) {
      if (!file_present(file)) {
        return ocr::Unavailable::NotDownloaded;
      }
    }
    return ocr::Unavailable::None;
  }

  [[nodiscard]] std::string unavailable_reason() const override {
    switch (unavailable()) {
      case ocr::Unavailable::Failed:
        return ocr_runtime_unavailable_reason();
      case ocr::Unavailable::NotDownloaded:
        return PATCHY_TRANSLATE_NOOP("QObject", "the text recognition models are not downloaded");
      case ocr::Unavailable::NoLanguages:
      case ocr::Unavailable::None:
        break;
    }
    return {};
  }

  [[nodiscard]] std::unique_ptr<ocr::Recognizer> open(std::string_view language_tag) const override {
    if (const auto reason = unavailable_reason(); !reason.empty()) {
      throw std::runtime_error(reason);
    }
    const auto* language = find_language(language_tag);
    if (language == nullptr) {
      throw std::runtime_error("the text recognition engine has no language \"" + std::string(language_tag) + "\"");
    }
    for (const auto file : {language->recognizer, language->dictionary}) {
      if (!file_present(file)) {
        throw std::runtime_error("the text recognition model file " + std::string(file) + " is missing");
      }
    }
    return std::make_unique<ModelRecognizer>(*language);
  }
};

}  // namespace

const std::vector<OcrModelLanguage>& ocr_model_languages() {
  return language_table();
}

std::vector<std::string_view> ocr_model_files() {
  std::vector<std::string_view> files{kOcrDetectorFile};
  for (const auto& language : language_table()) {
    for (const auto file : {language.recognizer, language.dictionary}) {
      if (std::find(files.begin(), files.end(), file) == files.end()) {
        files.push_back(file);
      }
    }
  }
  return files;
}

std::shared_ptr<ocr::Engine> ocr_model_engine() {
  if (!ocr_models_supported()) {
    return nullptr;
  }
  static const std::shared_ptr<ocr::Engine> engine = std::make_shared<ModelEngine>();
  return engine;
}

void register_ocr_model_engine() {
  static std::once_flag once;
  std::call_once(once, [] {
    if (ocr_model_engine() == nullptr) {
      return;
    }
    // Listed whether or not its files are there: the dialogs offer the download.
    ocr::add_engine_provider([] { return std::vector<std::shared_ptr<ocr::Engine>>{ocr_model_engine()}; });
  });
}

void set_ocr_model_default_language(std::string_view app_language) {
  // The app's language names a pick directly, or through its first part ("pt_BR" is a
  // Latin-script language, "zh_TW" is Traditional Chinese).
  std::string tag = "en";
  const auto base = app_language.substr(0, app_language.find_first_of("-_"));
  if (app_language == "zh_TW" || app_language == "zh-TW" || app_language == "zh-Hant") {
    tag = "zh-Hant";
  } else if (base == "zh") {
    tag = "zh-Hans";
  } else if (find_language(base) != nullptr) {
    tag = std::string(base);
  } else if (base == "de" || base == "es" || base == "fr" || base == "it" || base == "pl" || base == "pt") {
    tag = "latin";
  }
  const std::lock_guard lock(state_mutex());
  default_language_tag() = tag;
}

}  // namespace patchy::ai
