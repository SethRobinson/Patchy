#pragma once

#include "core/vector_shape.hpp"
#include "ui/preset_library.hpp"

#include <QPixmap>
#include <QString>

namespace patchy::ui {

// One stampable custom shape: the path is normalized to the unit box and
// mapped onto the drag rect at stamp time. `id` is the persisted identity
// ("shape.builtin.*" for bundled geometry, "shape.user.<uuid>" for Define
// Custom Shape); storage_id is the sidecar filename stem.
struct CustomShapeLibraryEntry {
  QString storage_id;
  QString id;
  QString name;
  QString folder;
  VectorPath path;
  QPixmap thumbnail;
};

struct CustomShapeLibraryTraits {
  using Entry = CustomShapeLibraryEntry;
  static constexpr const char* kSubdir = "shapes";
  static constexpr bool kHasFolders = true;
  static constexpr bool kUngroupedSortsFirst = true;
  static const QString& id(const Entry& entry) { return entry.storage_id; }
};

using CustomShapeLibraryBase = PresetLibraryT<CustomShapeLibraryTraits>;

class CustomShapeLibrary : public CustomShapeLibraryBase {
  Q_OBJECT

public:
  explicit CustomShapeLibrary(QString storage_dir = {}, QObject* parent = nullptr);

  QString add_shape(const QString& name, const VectorPath& path, const QString& folder = {},
                    const QString& preferred_storage_id = {}, const QString& shape_id = {});
  bool rename_shape(const QString& storage_id, const QString& name);
  bool remove_shape(const QString& storage_id);
  [[nodiscard]] const CustomShapeLibraryEntry* find_entry_by_shape_id(const QString& shape_id) const;

  // Writes any missing builtin shapes (first run and after updates add new
  // builtins; user deletions of builtins are respected via the settings gate
  // the caller owns).
  int restore_default_shapes();

private:
  void reload();
  bool write_sidecar(const CustomShapeLibraryEntry& entry) const;
};

// 40x40 outline thumbnail of a unit-box shape path.
[[nodiscard]] QPixmap custom_shape_thumbnail(const VectorPath& path, int size = 40);
[[nodiscard]] QString custom_shape_display_name(const CustomShapeLibraryEntry& entry);

}  // namespace patchy::ui
