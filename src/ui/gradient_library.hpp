#pragma once

#include "core/layer.hpp"
#include "ui/preset_library.hpp"

#include <QPixmap>
#include <QString>
#include <QStringList>

#include <optional>

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

// The current Foreground and Background colors a gradient preview draws a preset's
// Foreground/Background stops in (GitHub #581).
struct GradientPreviewColors {
  RgbColor foreground{0, 0, 0};
  RgbColor background{255, 255, 255};

  friend bool operator==(const GradientPreviewColors &,
                         const GradientPreviewColors &) = default;
};

class GradientLibrary : public GradientLibraryBase {
  Q_OBJECT
public:
  explicit GradientLibrary(QString storage_dir = {}, QObject *parent = nullptr);

  // The colors every entry's thumbnail draws its Foreground/Background stops in.
  // MainWindow passes the current pair on each color change; only entries with
  // such stops redraw, and nothing redraws when the pair is unchanged. It does
  // not emit changed(): the lists rebuild from the thumbnails when they open.
  void set_preview_colors(const GradientPreviewColors &colors);
  [[nodiscard]] const GradientPreviewColors &preview_colors() const noexcept {
    return preview_colors_;
  }

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
  [[nodiscard]] QPixmap thumbnail_for(const GradientDefinition &definition) const;

  GradientPreviewColors preview_colors_{};
};

// GRD presets may defer stops to the tool colors; shape fills store concrete
// colors, so resolve at pick time (shared with the options-bar paint pickers).
[[nodiscard]] GradientDefinition resolve_gradient_definition(GradientDefinition definition,
                                                             RgbColor foreground,
                                                             RgbColor background);
[[nodiscard]] bool gradient_has_dynamic_stops(const GradientDefinition &definition);

// A preset preview: alpha over the transparency checkerboard, Foreground/Background
// stops in `colors` (their stored placeholder colors without it).
[[nodiscard]] QPixmap
gradient_thumbnail(const GradientDefinition &definition, int width = 112,
                   int height = 28,
                   const std::optional<GradientPreviewColors> &colors = std::nullopt);

// Whether two gradients draw the same: form, smoothness, color stops (Foreground and
// Background ones resolved in `colors`) and alpha stops, within what a PSD or GRD
// round trip keeps; the name is ignored.
[[nodiscard]] bool gradient_definitions_draw_same(const GradientDefinition &left,
                                                  const GradientDefinition &right,
                                                  const GradientPreviewColors &colors);
// The storage id of the library entry `gradient` was made from, for a preset list
// to show it (GitHub #578, #579): the entries that draw the same with their
// Foreground/Background stops in `colors`, preferring one with the gradient's
// name, then one with Foreground/Background stops (a new Gradient Fill layer's
// Foreground to Background over Black to White on the default colors), then the
// first in library order. Empty when none matches.
[[nodiscard]] QString matching_gradient_library_entry(const GradientLibrary &library,
                                                      const GradientDefinition &gradient,
                                                      const GradientPreviewColors &colors);
[[nodiscard]] QString
gradient_library_entry_display_name(const GradientLibraryEntry &entry);
[[nodiscard]] QString gradient_folder_display_name(const QString &folder);

} // namespace patchy::ui
