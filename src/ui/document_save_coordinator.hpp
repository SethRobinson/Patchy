#pragma once

#include "ui/image_save_options.hpp"
#include <QString>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace patchy { class Document; }
namespace patchy::ui {
struct DocumentSession;
class SaveProgress;

// Classification shared by Save routing and the dialog preflight.
[[nodiscard]] bool is_photoshop_document_extension(const QString& extension);
[[nodiscard]] bool is_tiff_extension(const QString& extension);
[[nodiscard]] bool is_read_only_source_extension(const QString& extension);
[[nodiscard]] bool flat_save_discards_layers(const Document& document);
[[nodiscard]] bool save_discards_layers(const Document& document, const QString& extension);

// Window effects only. User-facing messages and prompts retain the MainWindow context.
class SaveHost {
public:
  virtual ~SaveHost() = default;
  [[nodiscard]] virtual DocumentSession* find_session(std::int64_t id) = 0;
  virtual bool save_as() = 0;
  virtual bool save_to_path(const QString& path) = 0;
  virtual bool commit_embedded(std::int64_t id) = 0;
  virtual void offer_download(const QString& path) = 0;
  virtual void remember_directory(const QString& path) = 0;
  virtual void persist_defaults(const ImageSaveOptions& options) = 0;
  virtual void add_recent(const QString& path) = 0;
  virtual void mark_saved(std::int64_t id) = 0;
  // Runs `write` with the Saving dialog (docs/file-formats.md, "Save progress"): on a
  // worker thread when `on_worker` (the writer then reads only its own snapshot), else
  // on this thread. `show_progress` false (scripted and unattended saves) never shows
  // the dialog. Rethrows what `write` throws.
  virtual void run_write(const QString& path, bool on_worker, bool show_progress,
                         const std::function<void(SaveProgress&)>& write) = 0;
};

struct DocumentSaveResult {
  bool flattened_copy{false};
  std::vector<std::string> notices;
};

// Synchronous save dispatch and session bookkeeping. No session pointer survives a host
// callback: session ids are resolved again before touching document or save state.
class DocumentSaveCoordinator {
public:
  explicit DocumentSaveCoordinator(SaveHost& host) : host_(host) {}
  bool save(std::int64_t session_id);
  [[nodiscard]] static ImageSaveOptions effective_options(
      const DocumentSession& session, const QString& path, const QString& extension,
      const std::optional<ImageSaveOptions>& requested, const ImageSaveOptions& defaults);
  // Called after text commit, playback stop, and data-loss/color confirmations. Throws
  // writer failures to the host's existing error handler; no saved state changes on failure.
  [[nodiscard]] std::optional<DocumentSaveResult> write_prepared(
      std::int64_t session_id, const QString& path, const QString& extension,
      const ImageSaveOptions& options, bool quiet, bool export_copy, bool discards_layers);
  static void mark_saved(DocumentSession& session);
private:
  SaveHost& host_;
};
}  // namespace patchy::ui
