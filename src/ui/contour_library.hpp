#pragma once

#include "core/layer.hpp"
#include "ui/preset_library.hpp"

#include <QString>
#include <QStringList>

namespace patchy::ui {

struct ContourLibraryEntry {
  QString storage_id;  // safe UUID filename stem
  QString name;
  StyleContour contour;  // contour.name mirrors `name`
};

struct ContourLibraryTraits {
  using Entry = ContourLibraryEntry;
  static constexpr const char* kSubdir = "contours";
  static constexpr bool kHasFolders = false;
  static constexpr bool kUngroupedSortsFirst = false;
  [[nodiscard]] static const QString& id(const Entry& entry) { return entry.storage_id; }
};

using ContourLibraryBase = PresetLibraryT<ContourLibraryTraits>;

// The user's saved layer-effect contours (GitHub issue 726), shown in the
// Layer Style contour pickers after the built-in shapes. Each entry is one
// single-contour Photoshop .shc file named by a storage UUID in
// <settings dir>/contours (the folder name and the one-contour-per-file layout
// are persisted contracts); the name lives in the file. Entries hold whole
// levels, as a .shc file does. Edits apply at once, with no undo, like the
// other preset libraries.
class ContourLibrary : public ContourLibraryBase {
  Q_OBJECT

public:
  // storage_dir is overridable for tests; empty = <settings dir>/contours.
  explicit ContourLibrary(QString storage_dir = {}, QObject* parent = nullptr);

  // The first entry (list order) that draws the same curve, or nullptr.
  [[nodiscard]] const ContourLibraryEntry* find_matching(const StyleContour& contour) const;

  // Adds one contour under `name` (trimmed; empty becomes "Contour N").
  // Returns the storage id, or empty on failure.
  QString add_contour(const QString& name, const StyleContour& contour);
  bool rename_contour(const QString& storage_id, const QString& name);
  bool remove_contour(const QString& storage_id);

  // Appends every contour of a Photoshop .shc file. A contour the library
  // already holds under the same name with the same curve is not added twice.
  // Returns the storage ids of the file's contours (added or already
  // present), empty with `error` set on failure.
  QStringList import_shc(const QString& path, QString& error, QStringList& warnings);

  // Writes the listed entries, in list order, into one .shc file (atomic).
  bool export_shc(const QStringList& storage_ids, const QString& path, QString& error) const;

private:
  void reload();
  [[nodiscard]] QString shc_path(const QString& storage_id) const;
  [[nodiscard]] bool write_entry(const ContourLibraryEntry& entry) const;
};

}  // namespace patchy::ui
