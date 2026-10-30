#pragma once

#include "core/vector_shape.hpp"
#include "ui/preset_library.hpp"

#include <QByteArray>
#include <QIcon>
#include <QPixmap>
#include <QString>
#include <QStringList>

#include <optional>
#include <vector>

class QComboBox;

namespace patchy::ui {

// One stampable custom shape: the path is normalized to the unit box and
// mapped onto the drag rect at stamp time. `id` is the persisted identity
// ("shape.builtin.*" for bundled geometry, "shape.user.<uuid>" for Define
// Custom Shape); storage_id is the sidecar filename stem. Bundled pack shapes
// ("shape.gameicons.*", GitHub #521) have no sidecar: storage_id is their id, they are
// read-only, and their thumbnail stays null (custom_shape_icon draws it when painted).
struct CustomShapeLibraryEntry {
  QString storage_id;
  QString id;
  QString name;
  QString folder;
  VectorPath path;
  QPixmap thumbnail;
  QString author;  // bundled shapes: the credited author
  bool bundled{false};
};

// The bundled game-icons.net pack (GitHub #521, docs/vector-tools.md): embedded in the app,
// read only when the picker, the Shapes Manager or a script first needs it.
inline constexpr const char* kBundledCustomShapesResource = ":/shapes/game-icons.json";
inline constexpr const char* kBundledCustomShapeIdPrefix = "shape.gameicons.";
[[nodiscard]] bool is_bundled_custom_shape_id(const QString& shape_id);
// The pack's shapes from its JSON (patchy-shape-pack version 1); entries that do not parse to
// a drawable outline are skipped. Empty, with `error` set, when the document is unusable.
[[nodiscard]] std::vector<CustomShapeLibraryEntry> read_bundled_custom_shapes(const QByteArray& json,
                                                                               QString* error = nullptr);
// The pack's folder names in display order (English; translated for display).
[[nodiscard]] const std::vector<const char*>& bundled_custom_shape_folders();

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
  // Moves every listed shape into `folder` (empty: ungrouped), emitting changed() once.
  // Returns how many moved; false-y writes leave that shape where it was.
  int move_shapes_to_folder(const QStringList& storage_ids, const QString& folder);
  // Renames a folder (every shape in it); an empty or unchanged name does nothing. Returns
  // how many shapes moved, emitting changed() once.
  int rename_folder(const QString& from, const QString& to);
  bool remove_shape(const QString& storage_id);
  // Removes every listed shape, emitting changed() once; returns the count.
  int remove_shapes(const QStringList& storage_ids);
  // A user copy ("<name> copy", same folder, fresh ids); empty on failure.
  QString duplicate_shape(const QString& storage_id);
  // Photoshop .csh import (GitHub issue 486, psd/csh_io.hpp): every decodable
  // shape becomes a user entry in a folder named after the file, emitting
  // changed() once. Returns the storage ids added; empty with a translated
  // `error` when the file is unusable. `warnings` lists skipped shapes.
  QStringList import_csh(const QString& path, QString& error, QStringList& warnings);
  [[nodiscard]] const CustomShapeLibraryEntry* find_entry_by_shape_id(const QString& shape_id) const;
  // Adds the bundled pack's read-only shapes the first time (emitting changed()); true when
  // this call loaded them. `resource` is for tests.
  bool ensure_bundled_shapes_loaded(const QString& resource = QString::fromLatin1(kBundledCustomShapesResource));
  [[nodiscard]] bool bundled_shapes_loaded() const noexcept { return bundled_loaded_; }

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
  bool bundled_loaded_{false};
  void reload();
  QString add_shape_without_signal(const QString& name, const VectorPath& path, const QString& folder,
                                   const QString& preferred_storage_id, const QString& shape_id);
  bool write_sidecar(const CustomShapeLibraryEntry& entry) const;
};

// 40x40 outline thumbnail of a unit-box shape path.
[[nodiscard]] QPixmap custom_shape_thumbnail(const VectorPath& path, int size = 40);
// The entry's thumbnail as an icon; a bundled shape's is drawn the first time it is painted
// at a size and cached, so a list of hundreds costs nothing until it scrolls into view.
[[nodiscard]] QIcon custom_shape_icon(const CustomShapeLibraryEntry& entry);
[[nodiscard]] QString custom_shape_display_name(const CustomShapeLibraryEntry& entry);
// The name, and for a bundled shape its author and license (the CC BY credit).
[[nodiscard]] QString custom_shape_tool_tip(const CustomShapeLibraryEntry& entry);
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
