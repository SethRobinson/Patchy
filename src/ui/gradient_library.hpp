#pragma once

#include "core/layer.hpp"
#include "ui/preset_library.hpp"

#include <QPixmap>
#include <QString>
#include <QStringList>

namespace patchy::ui {

inline constexpr int kDefaultGradientsVersion = 1;

struct GradientLibraryEntry {
  QString storage_id;
  QString name;
  QString folder;
  GradientDefinition definition;
  QPixmap thumbnail;
};

struct GradientLibraryTraits {
  using Entry = GradientLibraryEntry;
  static constexpr const char *kSubdir = "gradients";
  static constexpr bool kHasFolders = true;
  // Historical gradient order has no ungrouped-first rule (unlike the other
  // folder libraries); preserved as-is.
  static constexpr bool kUngroupedSortsFirst = false;
  [[nodiscard]] static const QString &id(const Entry &entry) {
    return entry.storage_id;
  }
};

using GradientLibraryBase = PresetLibraryT<GradientLibraryTraits>;

class GradientLibrary : public GradientLibraryBase {
  Q_OBJECT
public:
  explicit GradientLibrary(QString storage_dir = {}, QObject *parent = nullptr);

  QString add_gradient(const QString &name,
                       const GradientDefinition &definition,
                       const QString &folder = {},
                       const QString &preferred_storage_id = {});
  QString duplicate_gradient(const QString &storage_id);
  bool rename_gradient(const QString &storage_id, const QString &name);
  bool set_gradient_folder(const QString &storage_id, const QString &folder);
  bool remove_gradient(const QString &storage_id);
  int remove_gradients(const QStringList &storage_ids);

  QString import_grd(const QString &path, QString &error,
                     QStringList &warnings);
  bool export_grd(const QStringList &storage_ids, const QString &path,
                  QString &error) const;

  int restore_default_gradients(int newer_than_version = 0);
  int reset_default_gradients_to_factory();
  [[nodiscard]] bool
  has_all_default_gradients_introduced_after(int newer_than_version) const;
  [[nodiscard]] bool default_gradients_match_factory() const;

private:
  void reload();
  bool save_entry(const GradientLibraryEntry &entry) const;
  bool write_sidecar(const GradientLibraryEntry &entry) const;
  bool remove_entry_files(const QString &storage_id);
  [[nodiscard]] QString grd_path(const QString &storage_id) const;
};

[[nodiscard]] QPixmap gradient_thumbnail(const GradientDefinition &definition,
                                         int width = 112, int height = 28);
[[nodiscard]] QString
gradient_library_entry_display_name(const GradientLibraryEntry &entry);
[[nodiscard]] QString gradient_folder_display_name(const QString &folder);

} // namespace patchy::ui
