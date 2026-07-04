#pragma once

#include "core/brush_tip.hpp"

#include <QImage>
#include <QJsonObject>
#include <QObject>
#include <QPixmap>
#include <QSize>
#include <QString>

#include <memory>
#include <vector>

namespace patchy::ui {

// The reserved id of the built-in procedural round brush (no bitmap tip). It is not stored on
// disk; the picker lists it first and selecting it clears the canvas brush tip.
[[nodiscard]] const QString& builtin_round_brush_tip_id();

struct BrushTipEntry {
  QString id;        // storage filename stem (UUID); stable across sessions
  QString name;
  QString folder;    // organizational group; empty = ungrouped (listed first)
  double spacing{0.25};
  double base_angle_degrees{0.0};  // static tip rotation (Photoshop Brush Tip Shape angle)
  double base_roundness{100.0};    // static tip roundness percent, 1-100
  patchy::BrushDynamics dynamics{};
  QSize size;
  QPixmap thumbnail;
};

// The user's bitmap brush tip collection. Each tip is one grayscale PNG (the coverage mask,
// 255 = paints) plus a JSON sidecar {"name", "spacing"} in the storage directory, so a corrupt
// file only loses that one tip and users can drop files in by hand. Full masks load lazily;
// entries carry ready-to-draw thumbnails.
class BrushTipLibrary : public QObject {
  Q_OBJECT

public:
  // storage_dir is overridable for tests; empty = <settings dir>/brushes.
  explicit BrushTipLibrary(QString storage_dir = {}, QObject* parent = nullptr);

  [[nodiscard]] const QString& storage_dir() const noexcept;
  [[nodiscard]] const std::vector<BrushTipEntry>& entries() const noexcept;
  [[nodiscard]] const BrushTipEntry* find_entry(const QString& id) const;

  // Full-resolution tip for painting; cached after first load. Null for unknown/unreadable ids
  // and for the built-in round id.
  [[nodiscard]] std::shared_ptr<const patchy::BrushTip> tip(const QString& id) const;

  // Imports every sampled brush from a .abr file into a folder named after the file. Returns
  // the id of the first imported tip, or an empty string when nothing was imported (error is
  // set). Warnings collect per-brush skips.
  QString import_abr(const QString& path, QString& error, QStringList& warnings);

  // Adds a tip from a coverage mask image (any format; converted to grayscale, cropped to
  // content). Returns the new id, or empty when the mask is empty/unsaveable.
  QString add_tip(const QString& name, const QImage& coverage_mask, double spacing,
                  const QString& folder = {});

  // Re-adds missing built-in default tips (matched by name inside the defaults folder), so
  // deleted defaults are always recoverable. Only specs newer than `newer_than_version` are
  // considered: the default (0) restores everything (the manager's "Restore Default Brushes"
  // button), while the startup version gate passes the user's stored defaultTipsVersion so an
  // upgrade seeds newly added tips without resurrecting deliberately deleted older ones.
  // Returns the number restored; 0 = all present.
  int restore_default_tips(int newer_than_version = 0);

  // Applies the curated default-tip dynamics to existing built-in tips whose dynamics are still
  // untouched (one-shot migration under the brushes/defaultTipsVersion gate). Returns the
  // number of tips updated.
  int apply_default_tip_dynamics();

  bool rename_tip(const QString& id, const QString& name);
  bool remove_tip(const QString& id);
  // Removes every listed tip, emitting changed() once at the end. Returns the removed count.
  int remove_tips(const QStringList& ids);
  bool set_tip_spacing(const QString& id, double spacing);
  bool set_tip_folder(const QString& id, const QString& folder);
  // Persists the tip's dynamics + static tip shape (angle/roundness). The mask is untouched, so
  // the tip cache stays valid.
  bool set_tip_dynamics(const QString& id, const patchy::BrushDynamics& dynamics,
                        double base_angle_degrees, double base_roundness);
  // Folder names in display order (ungrouped tips are not a folder and sort first).
  [[nodiscard]] QStringList folders() const;

signals:
  void changed();

private:
  struct StoredTip;

  void reload();
  QString add_tip_internal(const QString& name, const QImage& coverage_mask, double spacing,
                           const QString& folder, const patchy::BrushDynamics& dynamics = {},
                           double base_angle_degrees = 0.0, double base_roundness = 100.0);
  bool remove_tip_internal(const QString& id);
  [[nodiscard]] QString png_path(const QString& id) const;
  [[nodiscard]] QString json_path(const QString& id) const;
  bool write_sidecar(const BrushTipEntry& entry) const;
  void sort_entries();

  QString storage_dir_;
  std::vector<BrushTipEntry> entries_;
  mutable std::vector<std::pair<QString, std::shared_ptr<const patchy::BrushTip>>> tip_cache_;
};

// Shared conversion helpers (also used by the manager dialog preview and define-brush capture).
[[nodiscard]] patchy::BrushTip brush_tip_from_coverage_image(const QImage& coverage_mask,
                                                             double spacing = 0.25);
[[nodiscard]] QImage coverage_image_from_brush_tip(const patchy::BrushTip& tip);
[[nodiscard]] QPixmap brush_tip_thumbnail(const patchy::BrushTip& tip, int extent);

// JSON (de)serialization for the sidecar "dynamics" object; exported for the popup and tests.
// Unknown keys/enum tokens read as defaults; seed/pen per-stroke inputs are never persisted.
[[nodiscard]] QJsonObject brush_dynamics_to_json(const patchy::BrushDynamics& dynamics);
[[nodiscard]] patchy::BrushDynamics brush_dynamics_from_json(const QJsonObject& object);
[[nodiscard]] bool brush_dynamics_is_default(const patchy::BrushDynamics& dynamics);

// True when the entry carries non-default dynamics or a non-default static tip shape.
[[nodiscard]] bool brush_tip_entry_has_dynamics(const BrushTipEntry& entry);
// The entry's thumbnail, with a small blue corner badge when it carries dynamics (used by the
// picker grid, the picker button face, and the manager tree so dynamic tips are recognizable).
[[nodiscard]] QPixmap brush_tip_thumbnail_with_badge(const BrushTipEntry& entry);

}  // namespace patchy::ui
