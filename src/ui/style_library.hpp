#pragma once

#include "core/layer.hpp"
#include "core/pattern_resource.hpp"
#include "psd/asl_io.hpp"

#include <QObject>
#include <QPixmap>
#include <QString>
#include <QStringList>

#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace patchy::ui {

// Bump when new built-in styles ship. MainWindow stores this in
// styles/defaultStylesVersion and passes the previous value to
// restore_default_styles(), so an upgrade adds only newly introduced defaults
// without resurrecting older defaults that the user deliberately deleted.
// Version 2 added the Materials folder (photo-texture styles); entries carry
// StylePreset::introduced_version.
inline constexpr int kDefaultStylesVersion = 2;

struct StyleLibraryEntry {
  QString storage_id;  // safe UUID filename stem; never written into .asl files
  QString id;          // style id ('Idnt'); persisted in exported .asl files
  QString source_id;   // original ASL id when `id` was remapped after a collision
  QString name;        // canonical resource name (built-in names stay English)
  QString folder;      // organizational group; empty = ungrouped
  LayerStyle style;
  std::optional<psd::AslBlendSettings> blend_settings;
  QPixmap thumbnail;  // kStyleThumbnailExtent card; views scale down
};

inline constexpr int kStyleThumbnailExtent = 96;

// Persistent application-wide layer-style preset collection, the style twin of
// PatternLibrary. Each entry is one single-style .asl file (self-contained:
// effects, optional blendOptions, and the referenced pattern tiles) plus a JSON
// sidecar {"id", "sourceId" (when remapped), "name", "folder"} and a cached
// thumbnail PNG in <settings dir>/styles. The storage UUID is deliberately
// separate from the style id so imported ids stay stable for re-import
// deduplication while filenames stay path-safe.
class StyleLibrary : public QObject {
  Q_OBJECT

public:
  // storage_dir is overridable for tests; empty = <settings dir>/styles.
  explicit StyleLibrary(QString storage_dir = {}, QObject* parent = nullptr);

  [[nodiscard]] const QString& storage_dir() const noexcept;
  [[nodiscard]] const std::vector<StyleLibraryEntry>& entries() const noexcept;
  [[nodiscard]] const StyleLibraryEntry* find_entry(const QString& storage_id) const;
  [[nodiscard]] const StyleLibraryEntry* find_entry_by_style_id(const QString& id) const;

  // The pattern tiles embedded in an entry's .asl file (lazy, small LRU cache).
  // Applying or previewing a style materializes these into the document.
  [[nodiscard]] std::vector<PatternResource> patterns_for_entry(const QString& storage_id) const;

  // Imports every decoded style from a Photoshop .asl file into a folder named
  // after the file. The original style id is retained when unique. Equal
  // payloads under an existing id deduplicate; a conflicting style under the
  // same id receives a fresh id and keeps its source id so re-importing
  // deduplicates the remapped entry. Returns the first imported/deduplicated
  // entry's storage id, or empty on failure.
  QString import_asl(const QString& path, QString& error, QStringList& warnings);

  // Exports the listed entries (tree order) plus the union of their embedded
  // pattern tiles into one .asl file. Returns false and sets `error` on failure.
  bool export_asl(const QStringList& storage_ids, const QString& path, QString& error) const;

  // Adds one style. preferred_style_id is retained when non-empty and unique;
  // an empty id generates a fresh GUID-shaped one. `patterns` carries the tiles
  // the style references (extra entries are dropped; missing references are
  // permitted and warn at apply time). Returns the storage id, or empty on
  // failure/id collision.
  QString add_style(const QString& name, const LayerStyle& style,
                    const std::optional<psd::AslBlendSettings>& blend_settings,
                    std::span<const PatternResource> patterns, const QString& folder = {},
                    const QString& preferred_style_id = {});

  // Creates an independent copy with a fresh storage UUID and style id.
  QString duplicate_style(const QString& storage_id);
  bool rename_style(const QString& storage_id, const QString& name);
  bool set_style_folder(const QString& storage_id, const QString& folder);
  bool remove_style(const QString& storage_id);
  // Removes every listed storage id, emitting changed() once. Returns the count.
  int remove_styles(const QStringList& storage_ids);

  // Re-adds missing built-ins introduced after newer_than_version. The default
  // argument restores every missing built-in (the manager button); startup
  // passes the stored defaultStylesVersion to respect deliberate deletions.
  int restore_default_styles(int newer_than_version = 0);

  // True when every built-in introduced after newer_than_version is installed.
  [[nodiscard]] bool has_all_default_styles_introduced_after(int newer_than_version) const;

  // True only when every shipped default exists with its canonical metadata,
  // effects, and embedded pattern tiles.
  [[nodiscard]] bool default_styles_match_factory() const;

  // Resets existing built-ins whose name, folder, or recipe differ from the
  // shipped definitions, retaining their storage ids and fixed style ids.
  int reset_default_styles_to_factory();

  // Folder names in display order; ungrouped styles are not a folder.
  [[nodiscard]] QStringList folders() const;

signals:
  void changed();

private:
  void reload();
  void sort_entries();
  QString add_style_internal(const QString& name, const LayerStyle& style,
                             const std::optional<psd::AslBlendSettings>& blend_settings,
                             std::span<const PatternResource> patterns, const QString& folder,
                             const QString& style_id, const QString& source_id = {});
  bool remove_style_internal(const QString& storage_id);
  [[nodiscard]] QString asl_path(const QString& storage_id) const;
  [[nodiscard]] QString json_path(const QString& storage_id) const;
  [[nodiscard]] QString thumbnail_path(const QString& storage_id) const;
  bool write_sidecar(const StyleLibraryEntry& entry) const;
  bool write_entry_asl(const StyleLibraryEntry& entry,
                       std::span<const PatternResource> patterns) const;
  [[nodiscard]] std::optional<psd::AslReadResult> read_entry_asl(const QString& storage_id) const;
  void refresh_thumbnail(StyleLibraryEntry& entry, std::span<const PatternResource> patterns,
                         bool save_cache);
  void invalidate_cached_patterns(const QString& storage_id) const;

  QString storage_dir_;
  std::vector<StyleLibraryEntry> entries_;
  mutable std::vector<std::pair<QString, std::vector<PatternResource>>> pattern_cache_;
};

// Localized folder name for one of the shipped folders ("Text", "Basics"),
// applied when defaults are seeded/reset; a user rename is kept verbatim.
[[nodiscard]] QString style_preset_folder_display_name(const char* english_folder);

// Canonical built-in names stay English in sidecars and exported .asl files.
// This helper translates one only while it is still equal to the shipped
// English name; a user rename is displayed verbatim.
[[nodiscard]] QString style_library_entry_display_name(const StyleLibraryEntry& entry);

// Renders the style applied to an "Aa" sample (rounded-square fallback when no
// font ink is available) over a neutral card. Shared by the library thumbnail
// cache, the Styles page browser, and the manager's large preview.
[[nodiscard]] QPixmap render_style_preview(const LayerStyle& style,
                                           const std::optional<psd::AslBlendSettings>& blend_settings,
                                           std::span<const PatternResource> patterns, int extent);

}  // namespace patchy::ui
