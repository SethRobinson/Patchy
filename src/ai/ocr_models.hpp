#pragma once

// The text recognition models (docs/ocr-engines.md, "PP-OCR"): PaddlePaddle's own ONNX
// exports of PP-OCR, one optional download. They run on the same ONNX Runtime as automatic
// selection; the implementation is in segmentation_engine_onnx.cpp, the only translation
// unit that includes ONNX Runtime. What goes into a network and what its output means is
// src/ocr (ocr_text_detect, ocr_text_decode); the engine that joins the two is
// ocr_model_engine.cpp. Builds without ONNX Runtime, and the web build, report the models
// unsupported.

#include "ai/segmentation_engine.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace patchy::ai {

// File names in the downloaded models folder (never renamed: the downloader, Preferences
// and the uninstaller name exactly these). Each recognizer has its character list beside
// it, the model's own inference.yml under the recognizer's name.
inline constexpr std::string_view kOcrDetectorFile = "ppocr_v6_small_det.onnx";
inline constexpr std::string_view kOcrMainRecognizerFile = "ppocr_v6_small_rec.onnx";

// True when this build can run the models at all (ONNX Runtime is part of it), whether or
// not the library loads on this machine and whether or not the models are downloaded.
[[nodiscard]] bool ocr_models_supported() noexcept;
// English reason ONNX Runtime cannot run here; empty when it loads.
[[nodiscard]] std::string ocr_runtime_unavailable_reason();
// Where `file` is looked for: the downloaded models folder only (empty while that is not
// set). A package never ships these files.
[[nodiscard]] std::filesystem::path ocr_model_path(std::string_view file);

struct OcrOutput {
  std::vector<float> values;
  std::vector<std::int64_t> shape;
};

// One run of the model `file` on one float tensor of `shape` (batch, channels, height,
// width): its first output. Throws std::runtime_error (an English diagnostic) when the
// model cannot be loaded or fails, and InferenceOutOfMemory when memory ran out.
[[nodiscard]] OcrOutput run_ocr_model(std::string_view file, const std::vector<float>& input,
                                      const std::array<std::int64_t, 4>& shape);

// Frees the sessions (before their files are deleted; they reload on next use).
void release_ocr_models();

}  // namespace patchy::ai
