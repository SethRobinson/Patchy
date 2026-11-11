#pragma once

// The Crop bar's Preset list (GitHub issue 696; docs/crop-tool.md "Ratio and Size"): one list holding
// the two free modes, the ratio presets, a few pixel sizes taken from the New Document
// templates, the user's saved presets and the Save / Delete commands. The rows and the rule
// that picks the row a bar state shows live here, apart from the widgets, so MainWindow only
// fills its combo from them.

#include <QSize>
#include <QSizeF>
#include <QString>
#include <QtCore/qnamespace.h>

#include <optional>
#include <vector>

namespace patchy::ui {

enum class CropPresetKind {
  FreeRatio,      // "Ratio": the W : H fields with no ratio set
  FreeSize,       // "Size": the Width / Height pixel fields
  OriginalRatio,  // the active document's ratio, read when it is picked
  Ratio,          // a built-in ratio
  Size,           // a pixel size from a New Document template
  UserRatio,      // a saved ratio
  UserSize,       // a saved pixel size
  Save,           // "Save Preset..."
  Delete,         // "Delete Preset"
};

// Item-data roles on the list (objectName cropPresetCombo): the row's CropPresetKind as an
// int, its ratio or pixel size (QSizeF), and a saved preset's id. A separator carries none.
// Exposed so UI tests can find rows without repeating the values.
inline constexpr int kCropPresetKindRole = Qt::UserRole;
inline constexpr int kCropPresetValueRole = Qt::UserRole + 1;
inline constexpr int kCropPresetIdRole = Qt::UserRole + 2;

struct CropPresetRow {
  CropPresetKind kind{CropPresetKind::FreeRatio};
  QString text;
  QSizeF value;     // the ratio or the pixel size; empty for the free modes and the commands
  QString user_id;  // saved presets only
  bool starts_group{false};  // a separator sits above this row
};

// A preset made with Save Preset: a ratio or a pixel size under a name.
struct CropUserPreset {
  QString id;  // "user-N", persisted; a new preset takes one above every existing N
  QString name;
  bool size{false};  // false: a ratio
  QSizeF value;
};

// tools/cropUserPresets (permanent): a JSON array, oldest first, of {id, name, kind ("ratio"
// or "size"), w, h}. Entries without a usable id, name, kind or value are skipped.
[[nodiscard]] std::vector<CropUserPreset> load_crop_user_presets();
void store_crop_user_presets(const std::vector<CropUserPreset>& presets);
// Appends a preset and returns its id. `name` must not be empty.
QString add_crop_user_preset(const QString& name, bool size, QSizeF value);
// False when no saved preset has the id.
bool remove_crop_user_preset(const QString& id);

// The New Document template ids whose sizes the list offers, in list order. A test checks
// that every id still names a template.
[[nodiscard]] const std::vector<QString>& crop_size_template_ids();

// The list top to bottom, in the current language.
[[nodiscard]] std::vector<CropPresetRow> crop_preset_rows();

// What the bar holds: the style, the ratio fields, the pending box and the document.
struct CropPresetState {
  bool size_style{false};
  QSizeF ratio;              // the W : H fields
  std::optional<QSize> box;  // the pending crop box; none without a session
  QSize document;            // empty without a document
};

// The row the list shows for a state: a saved preset before a built-in one with the same
// values, Original Ratio before the fixed ratios, and the free mode when nothing matches.
[[nodiscard]] int crop_preset_row_for_state(const std::vector<CropPresetRow>& rows, const CropPresetState& state);

// The document's ratio reduced by its greatest common divisor; empty for an empty size.
[[nodiscard]] QSizeF crop_original_ratio(QSize document);

// The names Save Preset offers: "3 : 2" and "1920 x 1080 px".
[[nodiscard]] QString crop_ratio_preset_name(QSizeF ratio);
[[nodiscard]] QString crop_size_preset_name(QSize size);

}  // namespace patchy::ui
