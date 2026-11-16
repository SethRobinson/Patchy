#pragma once

// The downloadable text recognition engine, id "ppocr" (docs/ocr-engines.md, "PP-OCR"):
// PP-OCR's detector and recognizers run through ONNX Runtime, joined to the ocr::Engine
// interface. Qt-free.
//
// Binding rules (the "Text recognition" entry of docs/legal-constraints-scanning.md): the
// recognizer is the one the user's language pick names; nothing here looks at an image to
// choose a model, a language or a font.

#include "ocr/ocr_engine.hpp"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace patchy::ai {

// A language pick and the recognizer it runs. Several picks share the main recognizer.
struct OcrModelLanguage {
  std::string_view tag;           // persisted in ocr/language: never renamed
  std::string_view display_name;  // English; the UI translates it
  std::string_view recognizer;    // file name in the downloaded models folder
  std::string_view dictionary;    // its character list (the model's inference.yml)
};

// Every pick, in display order.
[[nodiscard]] const std::vector<OcrModelLanguage>& ocr_model_languages();
// Every file the engine can use, each once: the detector, then each recognizer with its
// dictionary. What the downloader fetches and Preferences deletes.
[[nodiscard]] std::vector<std::string_view> ocr_model_files();

// The engine, or null in a build that cannot run the models (ocr_models_supported()).
[[nodiscard]] std::shared_ptr<ocr::Engine> ocr_model_engine();
// Adds the engine to ocr::engines() (once; later calls do nothing). Call at startup.
void register_ocr_model_engine();
// The language the engine preselects before the user has picked one: the app's own
// language as the UI knows it (a BCP-47 tag such as "ja" or "pt_BR"), never anything about
// an image. Unknown or empty means English.
void set_ocr_model_default_language(std::string_view app_language);

}  // namespace patchy::ai
