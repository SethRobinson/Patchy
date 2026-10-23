#pragma once

// The TrustMark watermark models (docs/watermarks.md) are one optional download: Adobe's
// decoders for variants Q, P and C, the variant Q encoder and Adobe's MIT license, fetched
// only when the user asks (File Info's Watermarks page, Image > Add Invisible Watermark,
// Preferences or app.downloadWatermarkModels) from Patchy's ai-models-3 release (Adobe's
// own host as the fallback), pinned by size and SHA-256, into the same per-user folder as
// the High Quality Object Selection model.

#include "ui/object_selection_model_download.hpp"

#include <QString>

#include <vector>

namespace patchy::ui {

// Keep in sync with trustmark_model_files() (src/ai/trustmark_watermark.cpp), the
// PATCHY_AI_WATERMARK_MODEL_FILES list in cmake/patchy_ai.cmake, scripts/setup-ai-deps.* and
// packaging/windows/UninstallPatchy.cs.
[[nodiscard]] const std::vector<DownloadableModelFile>& watermark_model_files();
[[nodiscard]] qint64 watermark_model_download_bytes();
// Whole megabytes, rounded, for messages.
[[nodiscard]] int watermark_model_download_megabytes();

// True when every watermark model file (the .onnx files; the license is not checked) is in
// `folder`.
[[nodiscard]] bool downloaded_watermark_models_present(const QString& folder);
// Deletes exactly the watermark model files from `folder` by their full names, then the
// folder only when that leaves it empty (a plain rmdir). Never a wildcard or recursive
// delete. The caller frees loaded sessions first (patchy::ai::release_trustmark_models).
bool delete_downloaded_watermark_models(const QString& folder, QString* error);

}  // namespace patchy::ui
