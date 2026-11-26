#pragma once

// The per-user asset library behind the Assets panel (docs/assets-panel.md, GitHub issue
// 751): layers the user saved for reuse. One whole PSD per asset plus one index file.
//
// Design limits recorded in docs/legal-constraints-interface.md ("Assets panel"): an asset is
// one whole local file written once, it keeps no reference to the document or layer it came
// from, and nothing here syncs, shares, suggests or classifies.
//
// Threads (docs/assets-panel.md, "Loading and thumbnails"): an AssetLibrary lives on the UI
// thread. The panel reads it through load_in_background and writes new assets through
// add_asset_in_background: one tracked worker reads the index, checks the asset files, and
// encodes and writes new assets, and hands each result back through a queued call that a
// destroyed library simply never receives. Everything else (rename, move, delete, the index
// write) is small and stays on the UI thread.

#include <QImage>
#include <QObject>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <vector>

class QMimeData;

namespace patchy::ui {

// A drag out of the Assets panel: the dragged asset ids, UTF-8, one per line. Permanent.
inline constexpr const char* kAssetDragMimeType = "application/x-patchy-asset-ids";
// The process that started the drag (its pid, decimal): a drag from another Patchy process
// is refused, as a layer drag is. Permanent.
inline constexpr const char* kAssetDragSourceMimeType = "application/x-patchy-asset-source";
// The category that always exists first. Its stored name is empty until the user renames
// it, and the panel shows a translated "General" for an empty name. Permanent.
inline constexpr const char* kDefaultAssetCategoryId = "default";

struct AssetEntry {
  QString id;
  QString name;
};

struct AssetCategory {
  QString id;
  QString name;
  std::vector<AssetEntry> assets;
};

// `<app data>/assets`, or the folder PATCHY_ASSETS_DIR names (isolated runs, the test
// suites). Empty when there is no per-user data folder.
[[nodiscard]] QString asset_library_directory();

// The asset ids a drag carries, in drag order; empty for any other mime data.
[[nodiscard]] QStringList asset_ids_from_mime_data(const QMimeData* mime_data);
// True when the drag started in this process.
[[nodiscard]] bool asset_drag_is_from_this_process(const QMimeData* mime_data);

// How often this process touched a file of an asset folder (an open, a stat, a folder
// listing, a write), split by thread. A diagnostic, like tracked_background_worker_count():
// the tests pin with it that startup opens no asset file and that the panel's reading happens
// off the UI thread.
struct AssetFileAccessCounts {
  int ui_thread{0};
  int worker{0};
};
void count_asset_file_access();
[[nodiscard]] AssetFileAccessCounts asset_file_access_counts();

class AssetLibrary final : public QObject {
  Q_OBJECT

public:
  // What a worker hands back for an asset being added: its PSD and thumbnail, or why not.
  struct EncodedAsset {
    std::vector<std::uint8_t> psd_bytes;
    QImage thumbnail;
    QString error;
  };

  explicit AssetLibrary(QString directory, QObject* parent = nullptr);
  ~AssetLibrary() override;

  [[nodiscard]] const QString& directory() const noexcept { return directory_; }
  [[nodiscard]] bool loaded() const noexcept { return loaded_; }

  // Names an asset found on disk when the index is missing or unreadable and the library
  // is rebuilt from its files (the window passes the saved layer's name). Set before a load;
  // it is called on the worker, so it must touch nothing but the file.
  void set_recovered_name_provider(std::function<QString(const QString& asset_path)> provider);

  // Starts reading the library on the worker; changed() follows when the index has landed,
  // and once more if the file check that runs after it leaves entries out. A no-op once the
  // library is loaded or loading.
  void load_in_background();
  // Reads the index and keeps every entry whose file is a PSD on disk, on the calling
  // thread; a missing, damaged or foreign file is left out and never breaks the rest. A
  // background load in flight is finished first, not read twice. Creates nothing on disk.
  void load();
  // For a caller that needs the library now (a change, placing): waits for the index a
  // background load is reading, or reads it. The background file check keeps running.
  void ensure_loaded();
  // True while the worker reads, checks or writes for this library (tests wait on it).
  [[nodiscard]] bool busy() const;
  // Waits for every asset being written in the background and lists it (window close: an
  // asset the user just added must not be left unlisted).
  void finish_pending_adds();

  [[nodiscard]] const std::vector<AssetCategory>& categories() const noexcept { return categories_; }
  [[nodiscard]] const AssetCategory* find_category(const QString& category_id) const;
  [[nodiscard]] const AssetEntry* find_asset(const QString& asset_id) const;
  // The id of the category holding the asset; empty when it is not in the library.
  [[nodiscard]] QString category_of(const QString& asset_id) const;
  [[nodiscard]] int asset_count() const;

  [[nodiscard]] QString asset_path(const QString& asset_id) const;
  [[nodiscard]] QString thumbnail_path(const QString& asset_id) const;
  [[nodiscard]] QString index_path() const;

  // Writes the asset's PSD and thumbnail and then the index, each through a QSaveFile, on
  // the calling thread. Returns the new asset's id, or nullopt with *error set and nothing
  // left behind.
  std::optional<QString> add_asset(const QString& category_id, const QString& name,
                                   std::span<const std::uint8_t> psd_bytes, const QImage& thumbnail,
                                   QString* error = nullptr);
  // The same with the expensive part on the worker: `encode` (called there once) makes the
  // PSD bytes and the thumbnail, the worker writes both files, and the asset is listed on the
  // UI thread when they are whole (asset_added). Assets queued this way appear in the order
  // they were queued. background_adds_finished follows when the queue is empty again.
  void add_asset_in_background(const QString& category_id, const QString& name, std::function<EncodedAsset()> encode);

  bool rename_asset(const QString& asset_id, const QString& name, QString* error = nullptr);
  bool remove_asset(const QString& asset_id, QString* error = nullptr);
  bool move_asset(const QString& asset_id, const QString& category_id, QString* error = nullptr);

  std::optional<QString> add_category(const QString& name, QString* error = nullptr);
  bool rename_category(const QString& category_id, const QString& name, QString* error = nullptr);
  // Deletes the category with the assets in it. The last category stays.
  bool remove_category(const QString& category_id, QString* error = nullptr);
  bool sort_category_by_name(const QString& category_id, QString* error = nullptr);

  // The stored thumbnail, read on the calling thread; null when the file is missing or
  // unreadable. The panel reads thumbnails through AssetThumbnailCache instead.
  [[nodiscard]] QImage thumbnail(const QString& asset_id) const;
  bool store_thumbnail(const QString& asset_id, const QImage& thumbnail);

  // The whole asset file; empty when it cannot be read.
  [[nodiscard]] QByteArray read_asset_bytes(const QString& asset_id) const;

  // A name as the library stores it: trimmed, single line, bounded; empty stays empty.
  [[nodiscard]] static QString cleaned_name(const QString& name);
  [[nodiscard]] static bool is_asset_id(const QString& text);

signals:
  void changed();
  // An asset written in the background is listed; `thumbnail` is its stored picture.
  void asset_added(const QString& asset_id, const QImage& thumbnail);
  // Assets left the library (deleted, a deleted category, files found missing).
  void assets_removed(const QStringList& asset_ids);
  // Every asset queued with add_asset_in_background is written or has failed: how many were
  // listed since the queue was last empty, and the last failure's text (empty when none).
  void background_adds_finished(int added, const QString& failure);

private:
  struct Shared;
  struct LoadJob;
  struct VerifyJob;
  struct AddJob;

  [[nodiscard]] AssetCategory* category(const QString& category_id);
  [[nodiscard]] bool save_index(QString* error);
  void ensure_default_category();
  void start_worker();
  void adopt_load(const std::shared_ptr<LoadJob>& job, bool queue_file_check = true);
  void queue_verification();
  void apply_verification(const std::shared_ptr<VerifyJob>& job);
  void remove_entries(const QStringList& asset_ids);
  void complete_add(const std::shared_ptr<AddJob>& job);
  [[nodiscard]] QString unused_id() const;

  QString directory_;
  bool loaded_{false};
  std::vector<AssetCategory> categories_;
  std::function<QString(const QString&)> recovered_name_provider_;
  std::shared_ptr<Shared> shared_;
  std::shared_ptr<LoadJob> load_job_;      // a background read not adopted yet
  std::shared_ptr<VerifyJob> verify_job_;  // the file check whose answer is still wanted
  std::vector<std::shared_ptr<AddJob>> add_jobs_;  // queued or being written, oldest first
  int background_added_{0};
  QString background_failure_;
};

}  // namespace patchy::ui
