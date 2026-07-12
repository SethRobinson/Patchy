#pragma once

#include "core/pattern_resource.hpp"

#include <QObject>
#include <QPixmap>
#include <QSize>
#include <QString>
#include <QStringList>

#include <optional>
#include <utility>
#include <vector>

namespace patchy::ui {

// Bump when new built-in patterns ship. MainWindow stores this in
// patterns/defaultPatternsVersion and passes the previous value to
// restore_default_patterns(), so an upgrade adds only newly introduced defaults
// without resurrecting older defaults that the user deliberately deleted.
// Version 2 added the bundled photo-texture presets (core PhotoPatternPreset
// table, per-entry introduced_version).
inline constexpr int kDefaultPatternsVersion = 2;

struct PatternLibraryEntry {
  QString storage_id;  // safe UUID filename stem; never written into PSDs
  QString id;          // Photoshop pattern id; persisted in layer styles/PSDs
  QString source_id;   // original PAT id when `id` was remapped after a collision
  QString name;        // canonical resource name (built-in names stay English)
  QString folder;      // organizational group; empty = ungrouped
  QSize size;
  QPixmap thumbnail;
};

// Persistent application-wide pattern collection. Each entry is one lossless
// RGBA PNG plus a JSON sidecar {"id", "sourceId" (when remapped), "name",
// "folder"} in
// <settings dir>/patterns. The storage UUID is deliberately separate from the
// Photoshop id: imported ids can contain path-unsafe text and must remain stable
// so importing the matching .pat can resolve an existing PSD reference.
class PatternLibrary : public QObject {
  Q_OBJECT

public:
  // storage_dir is overridable for tests; empty = <settings dir>/patterns.
  explicit PatternLibrary(QString storage_dir = {}, QObject* parent = nullptr);

  [[nodiscard]] const QString& storage_dir() const noexcept;
  [[nodiscard]] const std::vector<PatternLibraryEntry>& entries() const noexcept;
  [[nodiscard]] const PatternLibraryEntry* find_entry(const QString& storage_id) const;
  [[nodiscard]] const PatternLibraryEntry* find_entry_by_pattern_id(const QString& id) const;

  // Loads a full-resolution RGBA tile by its Photoshop id. PixelBuffer is
  // implicitly shared, so the returned resource is cheap to copy into a
  // document. Library resources are always Authored when materialized.
  [[nodiscard]] std::optional<PatternResource> resource(const QString& pattern_id) const;

  // Loads an exact library entry by its storage UUID. Callers that present a
  // specific manager row use this instead of resolving only by Photoshop id,
  // because an open document may already contain different pixels under that
  // same id.
  [[nodiscard]] std::optional<PatternResource> resource_for_entry(
      const QString& storage_id) const;

  // Imports every decoded pattern from a Photoshop .pat file into a folder
  // named after the file. The original Photoshop id is retained when unique.
  // Equal pixels under an existing id deduplicate; a conflicting tile under
  // the same id receives a fresh pattern UUID and a warning. Its source id is
  // retained so re-importing the same pixels deduplicates the remapped entry.
  // Returns the first imported/deduplicated entry's storage id, or empty on failure.
  QString import_pat(const QString& path, QString& error, QStringList& warnings);

  // Imports one image file (any format QImageReader reads, EXIF orientation
  // applied) as a single ungrouped pattern named after the file stem, with a
  // fresh pattern UUID. Alpha is preserved and any tile rectangle is allowed;
  // images over 8 million pixels are rejected. An animated image imports its
  // first frame with a warning. No pixel dedup: re-importing adds a new entry.
  // Returns the new entry's storage id, or empty on failure.
  QString import_image(const QString& path, QString& error, QStringList& warnings);

  // Adds one RGBA tile. preferred_pattern_id is retained when non-empty and
  // unique; an empty id generates a fresh Photoshop-shaped UUID. Returns the
  // safe storage id, or empty on failure/id collision.
  QString add_pattern(const QString& name, const PixelBuffer& tile, const QString& folder = {},
                      const QString& preferred_pattern_id = {});

  // Creates an independent copy with a fresh storage UUID and pattern UUID.
  // Returns its storage id, or empty on failure.
  QString duplicate_pattern(const QString& storage_id);
  bool rename_pattern(const QString& storage_id, const QString& name);
  bool set_pattern_folder(const QString& storage_id, const QString& folder);
  bool remove_pattern(const QString& storage_id);
  // Removes every listed storage id, emitting changed() once. Returns the count.
  int remove_patterns(const QStringList& storage_ids);

  // Re-adds missing built-ins introduced after newer_than_version. The default
  // argument restores every missing built-in (the manager button); startup
  // passes the stored defaultPatternsVersion to respect deliberate deletions.
  int restore_default_patterns(int newer_than_version = 0);

  // True when every built-in introduced after newer_than_version is installed.
  // Startup uses this after a restore attempt before advancing its version gate;
  // defaults from older versions may still be deliberately absent.
  [[nodiscard]] bool has_all_default_patterns_introduced_after(
      int newer_than_version) const;

  // True only when every shipped default exists with its canonical metadata
  // and generated pixels. The manager uses this to distinguish "already done"
  // from a partial restore that needs a writable-folder warning.
  [[nodiscard]] bool default_patterns_match_factory() const;

  // Resets existing built-ins whose name, folder, or pixels differ from the
  // shipped generators, retaining their storage ids and fixed Photoshop ids.
  // Pair with restore_default_patterns() for the manager's factory-restore action.
  int reset_default_patterns_to_factory();

  // Folder names in display order; ungrouped patterns are not a folder.
  [[nodiscard]] QStringList folders() const;

signals:
  void changed();

private:
  void reload();
  void sort_entries();
  QString add_pattern_internal(const QString& name, const PixelBuffer& tile,
                               const QString& folder, const QString& pattern_id,
                               const QString& source_id = {});
  bool remove_pattern_internal(const QString& storage_id);
  [[nodiscard]] QString png_path(const QString& storage_id) const;
  [[nodiscard]] QString json_path(const QString& storage_id) const;
  bool write_sidecar(const PatternLibraryEntry& entry) const;
  [[nodiscard]] std::optional<PixelBuffer> tile_for_entry(
      const PatternLibraryEntry& entry) const;
  void invalidate_cached_tile(const QString& storage_id) const;

  QString storage_dir_;
  std::vector<PatternLibraryEntry> entries_;
  mutable std::vector<std::pair<QString, PixelBuffer>> tile_cache_;
};

// Localized folder names used when defaults are first seeded/reset: the
// code-generated presets and the bundled photo textures each get their own.
[[nodiscard]] QString default_patterns_folder_name();
[[nodiscard]] QString photo_patterns_folder_name();

// Canonical built-in names stay English in sidecars and PSD descriptors. This
// helper translates one only while it is still equal to the shipped English
// name; a user rename is displayed verbatim.
[[nodiscard]] QString pattern_library_entry_display_name(const PatternLibraryEntry& entry);

// Checkerboard-backed, repeated-tile preview shared by pickers and the manager.
[[nodiscard]] QPixmap pattern_thumbnail(const PixelBuffer& tile, int extent = 48);

}  // namespace patchy::ui
