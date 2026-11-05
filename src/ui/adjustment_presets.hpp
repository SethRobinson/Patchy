#pragma once

// Adjustment dialog presets (#588, #589, #592, #528): the shipped Hue/Saturation and Levels
// presets, the user's own presets of every kind, and the button set the Hue/Saturation,
// Levels, Curves and Black & White dialogs share (docs/adjustment-presets.md).

#include "core/adjustment_layer.hpp"

#include <QString>
#include <QWidget>

#include <functional>
#include <optional>
#include <span>
#include <vector>

class QPushButton;

namespace patchy::ui {

enum class AdjustmentPresetKind { HueSaturation, Levels, Curves, BlackWhite };

// User presets live one file per preset, in the kind's Photoshop preset format (.ahu, .alv,
// .acv, .blw), named after the preset, in <app data>/adjustment-presets/<kind folder>. The
// folder names are permanent: hue-saturation, levels, curves, black-white. The environment
// variable PATCHY_ADJUSTMENT_PRESETS_DIR replaces the adjustment-presets root (tests).
[[nodiscard]] QString user_adjustment_presets_directory(AdjustmentPresetKind kind);
[[nodiscard]] QString adjustment_preset_suffix(AdjustmentPresetKind kind);
// The open/save dialog filter for the kind's file, e.g. "Hue/Saturation Preset (*.ahu)".
[[nodiscard]] QString adjustment_preset_file_filter(AdjustmentPresetKind kind);

struct UserAdjustmentPreset {
  QString name;
  QString path;
};
// Sorted by name, case-insensitively.
[[nodiscard]] std::vector<UserAdjustmentPreset> user_adjustment_presets(AdjustmentPresetKind kind);
// The file a preset name is stored in: characters a file name cannot hold become "_" and
// surrounding spaces and dots are dropped. Empty when nothing usable is left.
[[nodiscard]] QString user_adjustment_preset_path(AdjustmentPresetKind kind, const QString& name);

// Shipped presets. Ids are permanent; names and values are Patchy's own.
struct HueSaturationPreset {
  const char* id;
  const char* name;  // QT_TRANSLATE_NOOP("QObject", ...)
  HueSaturationAdjustment settings;
};
[[nodiscard]] std::span<const HueSaturationPreset> builtin_hue_saturation_presets();
// Whether two settings render alike: the colorize triple while colorizing, otherwise the
// master triple and every range (its limits and its triple).
[[nodiscard]] bool hue_saturation_presets_match(const HueSaturationAdjustment& left,
                                                const HueSaturationAdjustment& right);

struct LevelsPreset {
  const char* id;
  const char* name;  // QT_TRANSLATE_NOOP("QObject", ...)
  LevelsAdjustment settings;
};
[[nodiscard]] std::span<const LevelsPreset> builtin_levels_presets();
// Every record alike; the selected channel tab does not count.
[[nodiscard]] bool levels_presets_match(const LevelsAdjustment& left, const LevelsAdjustment& right);

// Save..., Delete, Import... and Export... for one kind's user presets. Save names the
// current settings and stores them; Delete removes the selected user preset after asking;
// Import adds a preset file to the user's presets; Export writes the current settings to a
// file of the kind's format. Object names: <prefix>SavePresetButton, ...DeletePresetButton,
// ...ImportPresetButton, ...ExportPresetButton. The four share one width; a Row fills the
// width its layout gives it, split evenly (place it under the preset combo, #588).
class AdjustmentPresetButtons final : public QWidget {
public:
  enum class Arrangement { Row, Column, Grid };

  struct Hooks {
    // Writes the dialog's current settings to `path` (throws on failure).
    std::function<void(const QString& path)> write_current;
    // Reads `path` as a preset of this kind (throws when it is not one); used by Import.
    std::function<void(const QString& path)> check_file;
    // The user preset the dialog's list has selected, if any (what Delete removes).
    std::function<std::optional<QString>()> selected_user_preset;
    // The user presets changed: rebuild the list and select `name` (empty: none). Import
    // also applies the new preset through this.
    std::function<void(const QString& name)> user_presets_changed;
  };

  AdjustmentPresetButtons(AdjustmentPresetKind kind, const QString& object_prefix, Arrangement arrangement,
                          Hooks hooks, QWidget* parent);

  // Call after the dialog's selection changes: Delete is enabled only on a user preset.
  void refresh();

  [[nodiscard]] QSize sizeHint() const override;
  [[nodiscard]] QSize minimumSizeHint() const override;

  [[nodiscard]] QPushButton* save_button() const noexcept { return save_; }
  [[nodiscard]] QPushButton* delete_button() const noexcept { return delete_; }
  [[nodiscard]] QPushButton* import_button() const noexcept { return import_; }
  [[nodiscard]] QPushButton* export_button() const noexcept { return export_; }

private:
  void save_preset();
  void delete_preset();
  void import_preset();
  void export_preset();
  // A Row's size: four buttons as wide as the widest one needs.
  [[nodiscard]] QSize row_size() const;

  AdjustmentPresetKind kind_;
  Arrangement arrangement_;
  QString object_prefix_;
  Hooks hooks_;
  QPushButton* save_{nullptr};
  QPushButton* delete_{nullptr};
  QPushButton* import_{nullptr};
  QPushButton* export_{nullptr};
};

}  // namespace patchy::ui
