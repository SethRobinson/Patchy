#pragma once

// The text recognition models (docs/ocr-engines.md, "PP-OCR") are one optional download:
// PaddlePaddle's own ONNX exports of PP-OCR, unchanged, with the Apache License text, fetched
// only when the user asks (the Download button a text recognition dialog or Preferences
// shows) from Patchy's ai-models-4 release (PaddlePaddle's own repositories as the fallback
// for each file), pinned by size and SHA-256, into the same per-user folder as the other
// downloaded models. Nothing downloads them unasked, and no script call does.

#include "ui/object_selection_model_download.hpp"

#include <QString>

#include <functional>
#include <vector>

class QWidget;

namespace patchy::ui {

// Keep in sync with ocr_model_files() (src/ai/ocr_model_engine.cpp), the
// PATCHY_AI_OCR_MODEL_FILES list in cmake/patchy_ai.cmake, scripts/setup-ai-deps.* and
// packaging/windows/UninstallPatchy.cs.
[[nodiscard]] const std::vector<DownloadableModelFile>& ocr_model_download_files();
[[nodiscard]] qint64 ocr_model_download_bytes();
// Whole megabytes, rounded, for messages.
[[nodiscard]] int ocr_model_download_megabytes();
// "100 MB from github.com/SethRobinson/Patchy (release ai-models-4)": what every offer of
// the download says about it, with the license line beside it.
[[nodiscard]] QString ocr_model_download_summary();

// True when every model file (not the license text) is in `folder`.
[[nodiscard]] bool downloaded_ocr_models_present(const QString& folder);
// Deletes exactly the text recognition files from `folder` by their full names, then the
// folder only when that leaves it empty (a plain rmdir). Never a wildcard or recursive
// delete. The caller frees loaded sessions first (patchy::ai::release_ocr_models).
bool delete_downloaded_ocr_models(const QString& folder, QString* error);

// What a dialog's Download button runs: the main window's download with its progress dialog
// over `parent`. True when the models are there afterwards. Installed once by the main
// window; empty (no window, or the web build) means the button is not offered.
using OcrModelDownloader = std::function<bool(QWidget* parent)>;
void set_ocr_model_downloader(OcrModelDownloader downloader);
[[nodiscard]] const OcrModelDownloader& ocr_model_downloader();

}  // namespace patchy::ui
