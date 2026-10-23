#pragma once

// Object Selection's High quality model (EfficientViT-SAM XL0, docs/ai-selection.md) is not
// packaged: desktop users download it on first use, from the ai-models-2 GitHub release,
// into the per-user app-data folder. Every file is pinned by size and SHA-256 and written
// through QSaveFile, so a partial or wrong download never replaces anything.

#include <QByteArray>
#include <QString>
#include <QUrl>

#include <functional>
#include <memory>
#include <vector>

class QObject;

namespace patchy::ui {

struct DownloadableModelFile {
  QString name;
  QUrl url;
  qint64 size{0};
  QByteArray sha256_hex;
  // Tried in order only when `url` fails (unreachable, an HTTP error, a wrong size or
  // checksum); every one serves the same pinned bytes.
  std::vector<QUrl> fallback_urls;
};

// Where a download comes from, for the text that offers it: "github.com/SethRobinson/Patchy
// (release ai-models-3)" for a GitHub release, otherwise the host of the first file's link.
[[nodiscard]] QString model_download_source_text(const std::vector<DownloadableModelFile>& files);

// The High quality model's files. Keep in sync with scripts/setup-ai-deps.* and the
// PATCHY_AI_DOWNLOAD_MODEL_FILES list in cmake/patchy_ai.cmake.
[[nodiscard]] const std::vector<DownloadableModelFile>& high_quality_model_files();
[[nodiscard]] qint64 high_quality_model_download_bytes();
// Where the download goes and the engine looks first: the engine's configured folder, by
// default <app data>/ai-models.
[[nodiscard]] QString downloaded_models_folder();
// Points the engine at downloaded_models_folder() and applies the saved Model setting.
// Desktop only; idempotent.
void configure_downloaded_object_selection_model();

// True when every High Quality file is in `folder` (what Preferences can delete).
[[nodiscard]] bool downloaded_high_quality_model_present(const QString& folder);
// Deletes exactly the High Quality model's files from `folder`, each by its full name, then
// the folder itself only when that leaves it empty (a plain rmdir). Never a wildcard or
// recursive delete. Missing files are fine; false with an English `error` when a present file
// could not be deleted. The caller frees loaded sessions first (patchy::ai::release_models).
// packaging/windows/UninstallPatchy.cs repeats the same names for uninstall.
bool delete_downloaded_high_quality_model(const QString& folder, QString* error);

// The Object Selection tool's Model setting (tools/objectSelectModel): true for High
// quality (used once downloaded), false for Standard.
[[nodiscard]] bool object_selection_high_quality_setting();
void set_object_selection_high_quality_setting(bool high_quality);

// What start_model_download reports while it runs, on the owner's thread: at the start of
// every file (file_received 0, before any byte), on every chunk, and when it moves to a
// fallback link.
struct ModelDownloadProgress {
  qint64 received{0};  // all files so far, skipped ones included
  qint64 total{0};
  int file_number{0};  // 1-based
  int file_count{0};
  QString file_name;
  qint64 file_size{0};
  qint64 file_received{0};
  QUrl url;  // the link being fetched now (the pinned or a fallback link, before redirects)
};

struct ModelDownloadResult {
  bool ok{false};
  bool cancelled{false};
  QString error;  // English diagnostic when neither ok nor cancelled
};

class ModelDownload {
public:
  virtual ~ModelDownload() = default;
  // Stops the transfer; finished() then reports cancelled. Nothing partial is kept.
  virtual void cancel() = 0;
};

// Downloads `files` into `folder` one after another (files already present with the right
// size and hash are skipped), each from its url and then its fallback_urls until one serves
// the pinned bytes. progress and finished run on `owner`'s thread and never after `owner` is
// destroyed. Redirects are followed by hand so every host, including the redirect target,
// resolves on a detached thread first (host_resolve.hpp).
[[nodiscard]] std::shared_ptr<ModelDownload> start_model_download(
    QObject* owner, QString folder, std::vector<DownloadableModelFile> files,
    std::function<void(const ModelDownloadProgress&)> progress, std::function<void(ModelDownloadResult)> finished);

}  // namespace patchy::ui
