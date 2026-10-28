#pragma once

#include "core/vector_shape.hpp"
#include "ui/preset_library.hpp"

#include <QPixmap>
#include <QString>
#include <QStringList>

#include <optional>

class QComboBox;

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
  // An empty folder ungroups the shape.
  bool set_shape_folder(const QString& storage_id, const QString& folder);
  bool remove_shape(const QString& storage_id);
  // Removes every listed shape, emitting changed() once; returns the count.
  int remove_shapes(const QStringList& storage_ids);
  // A user copy ("<name> copy", same folder, fresh ids); empty on failure.
  QString duplicate_shape(const QString& storage_id);
  [[nodiscard]] const CustomShapeLibraryEntry* find_entry_by_shape_id(const QString& shape_id) const;

  // Writes the missing builtin shapes introduced after newer_than_version
  // (0 = all of them; the caller's shapes/defaultShapesVersion stamp keeps
  // deleted defaults deleted) and refreshes the stored geometry of builtins
  // whose path changed in code (renames are kept). Returns the number of
  // shapes added; geometry refreshes do not count.
  int restore_default_shapes(int newer_than_version = 0);
  // Puts renamed or moved builtins back under their factory name and folder.
  int reset_default_shapes_to_factory();
  [[nodiscard]] bool has_all_default_shapes_introduced_after(int version) const;
  [[nodiscard]] bool default_shapes_match_factory() const;

private:
  void reload();
  bool write_sidecar(const CustomShapeLibraryEntry& entry) const;
};

// 40x40 outline thumbnail of a unit-box shape path.
[[nodiscard]] QPixmap custom_shape_thumbnail(const VectorPath& path, int size = 40);
[[nodiscard]] QString custom_shape_display_name(const CustomShapeLibraryEntry& entry);
// A folder name as shown: the builtin folders (Arrows, Symbols) translated.
[[nodiscard]] QString custom_shape_folder_display_name(const QString& folder);
// Scales a path into the unit box (the library's storage form), keeping its
// proportions; false when it is empty or too small to define a shape.
bool normalize_custom_shape_path(VectorPath& path);
// Every drawable shape of an SVG file merged into one unit-box path (paint
// ignored, holes kept as their combine ops): Define Custom Shape from SVG
// File and the Custom Shapes manager's import. nullopt with a translated
// error when the file cannot be read or holds no shape geometry.
[[nodiscard]] std::optional<VectorPath> custom_shape_path_from_svg_file(const QString& path, QString& error);
// Fills the Shape picker (the options bar's and the Create Custom Shape
// dialog's): every library shape with its thumbnail and display name, the
// shape id as item data. Keeps the current shape when it is still listed.
void populate_custom_shape_combo(QComboBox& combo, const CustomShapeLibrary& library);

}  // namespace patchy::ui
