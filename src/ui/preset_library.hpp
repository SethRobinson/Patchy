#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

class QImage;

namespace patchy {
class PixelBuffer;
}

namespace patchy::ui {

// QObject base of the preset libraries (styles, patterns, brush tips,
// gradients, filter looks): owns the storage directory and the shared
// changed() signal. The per-entry plumbing lives in PresetLibraryT below.
class PresetLibraryBase : public QObject {
  Q_OBJECT

 public:
  [[nodiscard]] const QString& storage_dir() const noexcept { return storage_dir_; }

 signals:
  // Emitted once after any mutation that changed the library (adds, renames,
  // folder moves, removals, default restores/resets).
  void changed();

 protected:
  // An empty storage_dir resolves to <settings dir>/<subdir> (overridable for tests).
  explicit PresetLibraryBase(QString storage_dir, const char* subdir, QObject* parent);

  // <storage dir>/<id><suffix>; suffix includes the dot (".asl", ".png", ...).
  [[nodiscard]] QString storage_path(const QString& id, const char* suffix) const;
  [[nodiscard]] QString json_path(const QString& id) const;

  QString storage_dir_;
};

// The CRUD/sidecar skeleton the preset libraries share. TraitsT provides:
//   using Entry = ...;                           // must have .name (+ .folder when kHasFolders)
//   static constexpr const char* kSubdir;        // storage subdir under the settings dir
//   static constexpr bool kHasFolders;           // Entry has a .folder member
//   static constexpr bool kUngroupedSortsFirst;  // empty folder sorts before named folders
//   static const QString& id(const Entry&);      // storage filename stem
// Persistence deltas stay per class and are passed in as callables (which files a
// write touches, extra sidecar keys, whether a no-op edit still rewrites): the
// on-disk sidecar bytes are persisted contracts and are never normalized here.
// Member functions instantiate lazily, so a library only needs the traits/members
// for the operations it actually uses (FilterLookLibrary has no folders).
template <typename TraitsT>
class PresetLibraryT : public PresetLibraryBase {
 public:
  using Entry = typename TraitsT::Entry;

  [[nodiscard]] const std::vector<Entry>& entries() const noexcept { return entries_; }

  [[nodiscard]] const Entry* find_entry(const QString& id) const {
    return find_entry_if([&id](const Entry& entry) { return TraitsT::id(entry) == id; });
  }

  // Folder names in display order; ungrouped entries are not a folder.
  [[nodiscard]] QStringList folders() const {
    QStringList result;
    for (const auto& entry : entries_) {
      if (!entry.folder.isEmpty() && !result.contains(entry.folder)) {
        result.append(entry.folder);
      }
    }
    return result;
  }

 protected:
  explicit PresetLibraryT(QString storage_dir, QObject* parent)
      : PresetLibraryBase(std::move(storage_dir), TraitsT::kSubdir, parent) {}

  template <typename Predicate>
  [[nodiscard]] const Entry* find_entry_if(Predicate&& predicate) const {
    const auto found = std::find_if(entries_.begin(), entries_.end(), predicate);
    return found == entries_.end() ? nullptr : &*found;
  }

  [[nodiscard]] typename std::vector<Entry>::iterator entry_iterator(const QString& id) {
    return std::find_if(entries_.begin(), entries_.end(),
                        [&id](const Entry& entry) { return TraitsT::id(entry) == id; });
  }

  void sort_entries() {
    std::sort(entries_.begin(), entries_.end(), [](const Entry& a, const Entry& b) {
      if constexpr (TraitsT::kHasFolders) {
        if constexpr (TraitsT::kUngroupedSortsFirst) {
          if (a.folder.isEmpty() != b.folder.isEmpty()) {
            return a.folder.isEmpty();
          }
        }
        const auto folder_order = QString::compare(a.folder, b.folder, Qt::CaseInsensitive);
        if (folder_order != 0) {
          return folder_order < 0;
        }
      }
      const auto name_order = QString::compare(a.name, b.name, Qt::CaseInsensitive);
      if (name_order != 0) {
        return name_order < 0;
      }
      return TraitsT::id(a) < TraitsT::id(b);
    });
  }

  // Rename skeleton: trims and validates, persists through `write` (sidecar
  // only, or payload+sidecar for gradients), then commits in memory, re-sorts,
  // and emits changed(). skip_unchanged keeps each class's historical no-op
  // behavior (styles/patterns return early; brush and gradient still rewrite).
  template <typename WriteEntryFn>
  bool rename_entry(const QString& id, const QString& name, bool skip_unchanged,
                    WriteEntryFn&& write) {
    const auto trimmed = name.trimmed();
    const auto found = entry_iterator(id);
    if (found == entries_.end() || trimmed.isEmpty()) {
      return false;
    }
    if (skip_unchanged && found->name == trimmed) {
      return true;
    }
    auto updated = *found;
    updated.name = trimmed;
    if (!write(updated)) {
      return false;
    }
    found->name = trimmed;
    sort_entries();
    emit changed();
    return true;
  }

  template <typename WriteEntryFn>
  bool set_entry_folder(const QString& id, const QString& folder, bool skip_unchanged,
                        WriteEntryFn&& write) {
    const auto found = entry_iterator(id);
    if (found == entries_.end()) {
      return false;
    }
    const auto trimmed = folder.trimmed();
    if (skip_unchanged && found->folder == trimmed) {
      return true;
    }
    auto updated = *found;
    updated.folder = trimmed;
    if (!write(updated)) {
      return false;
    }
    found->folder = trimmed;
    sort_entries();
    emit changed();
    return true;
  }

  // Removal skeleton; remove_files performs the per-class file deletions and
  // cache invalidation and reports whether the entry may leave the list.
  template <typename RemoveFilesFn>
  bool remove_entry_internal(const QString& id, RemoveFilesFn&& remove_files) {
    const auto found = entry_iterator(id);
    if (found == entries_.end()) {
      return false;
    }
    if (!remove_files(id)) {
      return false;
    }
    entries_.erase(found);
    return true;
  }

  template <typename RemoveFilesFn>
  bool remove_entry(const QString& id, RemoveFilesFn&& remove_files) {
    if (!remove_entry_internal(id, remove_files)) {
      return false;
    }
    emit changed();
    return true;
  }

  // Removes every listed id, emitting changed() once. Returns the count.
  template <typename RemoveFilesFn>
  int remove_entries(const QStringList& ids, RemoveFilesFn&& remove_files) {
    int removed = 0;
    for (const auto& id : ids) {
      if (remove_entry_internal(id, remove_files)) {
        ++removed;
      }
    }
    if (removed > 0) {
      emit changed();
    }
    return removed;
  }

  std::vector<Entry> entries_;
};

}  // namespace patchy::ui

// Helpers previously duplicated file-scope across the library TUs. Nested
// namespace: layer_style_dialog.cpp still carries an anonymous-namespace
// pattern_tiles_equal, so these must not collide at patchy::ui scope.
namespace patchy::ui::presets {

[[nodiscard]] QString qstring_from_utf8(const std::string& text);
[[nodiscard]] std::string utf8_from_qstring(const QString& text);

// Dimension/format/byte equality of two RGBA tiles.
[[nodiscard]] bool pattern_tiles_equal(const PixelBuffer& lhs, const PixelBuffer& rhs);

// Atomic (QSaveFile) PNG write; false when the image is null or any step fails.
[[nodiscard]] bool save_png(const QImage& image, const QString& path);

}  // namespace patchy::ui::presets
