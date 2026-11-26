#pragma once

// The per-user asset library behind the Assets panel (docs/assets-panel.md, GitHub issue
// 751): layers the user saved for reuse. One whole PSD per asset plus one index file.
//
// Design limits recorded in docs/legal-constraints-interface.md ("Assets panel"): an asset is
// one whole local file written once, it keeps no reference to the document or layer it came
// from, and nothing here syncs, shares, suggests or classifies.

#include <QImage>
#include <QObject>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <functional>
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

class AssetLibrary final : public QObject {
  Q_OBJECT

public:
  explicit AssetLibrary(QString directory, QObject* parent = nullptr);

  [[nodiscard]] const QString& directory() const noexcept { return directory_; }
  [[nodiscard]] bool loaded() const noexcept { return loaded_; }

  // Names an asset found on disk when the index is missing or unreadable and the library
  // is rebuilt from its files (the window passes the saved layer's name). Set before load.
  void set_recovered_name_provider(std::function<QString(const QString& asset_path)> provider);

  // Reads the index and keeps every entry whose file is a PSD on disk; a missing, damaged
  // or foreign file is left out and never breaks the rest. Creates nothing on disk.
  void load();
  void ensure_loaded();

  [[nodiscard]] const std::vector<AssetCategory>& categories() const noexcept { return categories_; }
  [[nodiscard]] const AssetCategory* find_category(const QString& category_id) const;
  [[nodiscard]] const AssetEntry* find_asset(const QString& asset_id) const;
  // The id of the category holding the asset; empty when it is not in the library.
  [[nodiscard]] QString category_of(const QString& asset_id) const;
  [[nodiscard]] int asset_count() const;

  [[nodiscard]] QString asset_path(const QString& asset_id) const;
  [[nodiscard]] QString thumbnail_path(const QString& asset_id) const;
  [[nodiscard]] QString index_path() const;

  // Writes the asset's PSD and thumbnail and then the index, each through a QSaveFile.
  // Returns the new asset's id, or nullopt with *error set and nothing left behind.
  std::optional<QString> add_asset(const QString& category_id, const QString& name,
                                   std::span<const std::uint8_t> psd_bytes, const QImage& thumbnail,
                                   QString* error = nullptr);
  bool rename_asset(const QString& asset_id, const QString& name, QString* error = nullptr);
  bool remove_asset(const QString& asset_id, QString* error = nullptr);
  bool move_asset(const QString& asset_id, const QString& category_id, QString* error = nullptr);

  std::optional<QString> add_category(const QString& name, QString* error = nullptr);
  bool rename_category(const QString& category_id, const QString& name, QString* error = nullptr);
  // Deletes the category with the assets in it. The last category stays.
  bool remove_category(const QString& category_id, QString* error = nullptr);
  bool sort_category_by_name(const QString& category_id, QString* error = nullptr);

  // The cached thumbnail; null when the file is missing or unreadable.
  [[nodiscard]] QImage thumbnail(const QString& asset_id) const;
  bool store_thumbnail(const QString& asset_id, const QImage& thumbnail);

  // The whole asset file; empty when it cannot be read.
  [[nodiscard]] QByteArray read_asset_bytes(const QString& asset_id) const;

  // A name as the library stores it: trimmed, single line, bounded; empty stays empty.
  [[nodiscard]] static QString cleaned_name(const QString& name);
  [[nodiscard]] static bool is_asset_id(const QString& text);

signals:
  void changed();

private:
  [[nodiscard]] AssetCategory* category(const QString& category_id);
  [[nodiscard]] bool save_index(QString* error);
  void rebuild_from_files();
  void ensure_default_category();

  QString directory_;
  bool loaded_{false};
  std::vector<AssetCategory> categories_;
  std::function<QString(const QString&)> recovered_name_provider_;
};

}  // namespace patchy::ui
