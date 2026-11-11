#include "ui/crop_presets.hpp"

#include "ui/app_settings.hpp"
#include "ui/new_document_dialog.hpp"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QSettings>

#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>

namespace patchy::ui {

namespace {

// Persisted identifiers (docs/crop-tool.md "Ratio and Size"): never rename the key, the field names
// or the kind tokens.
const QString kUserPresetsKey = QStringLiteral("tools/cropUserPresets");
constexpr QLatin1StringView kKindRatio("ratio");
constexpr QLatin1StringView kKindSize("size");
constexpr QLatin1StringView kUserIdPrefix("user-");

constexpr double kMaxRatio = 10000.0;  // the ratio fields' range
constexpr int kMaxSize = 30000;        // the Size fields' range

struct BuiltInRatio {
  const char* text;  // translated in the MainWindow context when it holds a word
  bool translated;
  double width;
  double height;
};

// Patchy's ratio presets, in list order (the ones the Ratio list offered before issue 696).
constexpr std::array<BuiltInRatio, 5> kBuiltInRatios{{
    {QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "1 : 1 (Square)"), true, 1.0, 1.0},
    {"4 : 5 (8 : 10)", false, 4.0, 5.0},
    {"5 : 7", false, 5.0, 7.0},
    {"2 : 3 (4 : 6)", false, 2.0, 3.0},
    {"16 : 9", false, 16.0, 9.0},
}};

bool same_value(QSizeF a, QSizeF b) {
  return std::abs(a.width() - b.width()) < 1e-9 && std::abs(a.height() - b.height()) < 1e-9;
}

QString main_window_text(const char* source) {
  return QCoreApplication::translate("patchy::ui::MainWindow", source);
}

// At most two decimals, trailing zeros dropped, as the ratio fields show a value.
QString trimmed_number(double value) {
  auto text = QString::number(value, 'f', 2);
  while (text.endsWith(QLatin1Char('0'))) {
    text.chop(1);
  }
  if (text.endsWith(QLatin1Char('.'))) {
    text.chop(1);
  }
  return text;
}

}  // namespace

std::vector<CropUserPreset> load_crop_user_presets() {
  std::vector<CropUserPreset> presets;
  const auto array = QJsonDocument::fromJson(app_settings().value(kUserPresetsKey).toString().toUtf8()).array();
  for (const auto& value : array) {
    const auto entry = value.toObject();
    CropUserPreset preset;
    preset.id = entry.value(QStringLiteral("id")).toString();
    preset.name = entry.value(QStringLiteral("name")).toString().simplified();
    const auto kind = entry.value(QStringLiteral("kind")).toString();
    const auto width = entry.value(QStringLiteral("w")).toDouble();
    const auto height = entry.value(QStringLiteral("h")).toDouble();
    if (!preset.id.startsWith(kUserIdPrefix) || preset.name.isEmpty() || (kind != kKindRatio && kind != kKindSize) ||
        !std::isfinite(width) || !std::isfinite(height) || width <= 0.0 || height <= 0.0) {
      continue;
    }
    preset.size = kind == kKindSize;
    if (preset.size) {
      preset.value = QSizeF(std::clamp(static_cast<int>(std::lround(width)), 1, kMaxSize),
                            std::clamp(static_cast<int>(std::lround(height)), 1, kMaxSize));
    } else {
      preset.value = QSizeF(std::min(width, kMaxRatio), std::min(height, kMaxRatio));
    }
    presets.push_back(std::move(preset));
  }
  return presets;
}

void store_crop_user_presets(const std::vector<CropUserPreset>& presets) {
  QJsonArray array;
  for (const auto& preset : presets) {
    QJsonObject entry;
    entry.insert(QStringLiteral("id"), preset.id);
    entry.insert(QStringLiteral("name"), preset.name);
    entry.insert(QStringLiteral("kind"), QString(preset.size ? kKindSize : kKindRatio));
    entry.insert(QStringLiteral("w"), preset.value.width());
    entry.insert(QStringLiteral("h"), preset.value.height());
    array.append(entry);
  }
  auto settings = app_settings();
  if (presets.empty()) {
    settings.remove(kUserPresetsKey);
  } else {
    settings.setValue(kUserPresetsKey, QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact)));
  }
}

QString add_crop_user_preset(const QString& name, bool size, QSizeF value) {
  auto presets = load_crop_user_presets();
  int next = 1;
  for (const auto& preset : presets) {
    bool numbered = false;
    const auto number = preset.id.mid(kUserIdPrefix.size()).toInt(&numbered);
    if (numbered) {
      next = std::max(next, number + 1);
    }
  }
  CropUserPreset preset;
  preset.id = kUserIdPrefix + QString::number(next);
  preset.name = name.simplified();
  preset.size = size;
  preset.value = value;
  const auto id = preset.id;
  presets.push_back(std::move(preset));
  store_crop_user_presets(presets);
  return id;
}

bool remove_crop_user_preset(const QString& id) {
  auto presets = load_crop_user_presets();
  const auto removed = std::erase_if(presets, [&id](const CropUserPreset& preset) { return preset.id == id; });
  if (removed == 0) {
    return false;
  }
  store_crop_user_presets(presets);
  return true;
}

const std::vector<QString>& crop_size_template_ids() {
  // Screen and social sizes people crop to by exact pixels. Print sizes are left to the ratio
  // presets: a size preset sets the box in pixels and never resamples.
  static const std::vector<QString> ids{
      QStringLiteral("screen-720p"),        QStringLiteral("screen-1080p"),
      QStringLiteral("screen-4k"),          QStringLiteral("social-square-1080"),
      QStringLiteral("social-portrait-1080x1350"), QStringLiteral("phone-story-1080x1920"),
  };
  return ids;
}

std::vector<CropPresetRow> crop_preset_rows() {
  std::vector<CropPresetRow> rows;
  rows.push_back({CropPresetKind::FreeRatio, main_window_text(QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Ratio")),
                  {}, {}, false});
  rows.push_back({CropPresetKind::FreeSize, main_window_text(QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Size")),
                  {}, {}, false});
  rows.push_back({CropPresetKind::OriginalRatio,
                  main_window_text(QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Original Ratio")), {}, {}, true});
  for (const auto& ratio : kBuiltInRatios) {
    rows.push_back({CropPresetKind::Ratio, ratio.translated ? main_window_text(ratio.text) : QString::fromLatin1(ratio.text),
                    QSizeF(ratio.width, ratio.height), {}, false});
  }
  // The pixel sizes come from the New Document templates, so there is one table of sizes.
  const auto templates = artboard_size_presets();
  bool first_size = true;
  for (const auto& id : crop_size_template_ids()) {
    const auto found = std::find_if(templates.begin(), templates.end(),
                                    [&id](const ArtboardSizePreset& preset) { return preset.id == id; });
    if (found == templates.end()) {
      continue;
    }
    rows.push_back({CropPresetKind::Size,
                    QStringLiteral("%1 (%2)").arg(crop_size_preset_name(found->size), found->name),
                    QSizeF(found->size), {}, first_size});
    first_size = false;
  }
  bool first_user = true;
  for (const auto& preset : load_crop_user_presets()) {
    rows.push_back({preset.size ? CropPresetKind::UserSize : CropPresetKind::UserRatio, preset.name, preset.value,
                    preset.id, first_user});
    first_user = false;
  }
  rows.push_back({CropPresetKind::Save,
                  main_window_text(QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Save Preset...")), {}, {}, true});
  rows.push_back({CropPresetKind::Delete,
                  main_window_text(QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Delete Preset")), {}, {}, false});
  return rows;
}

int crop_preset_row_for_state(const std::vector<CropPresetRow>& rows, const CropPresetState& state) {
  const auto find = [&rows](CropPresetKind kind, const std::optional<QSizeF>& value = std::nullopt) {
    for (std::size_t index = 0; index < rows.size(); ++index) {
      if (rows[index].kind == kind && (!value.has_value() || same_value(rows[index].value, *value))) {
        return static_cast<int>(index);
      }
    }
    return -1;
  };
  if (state.size_style) {
    if (state.box.has_value() && !state.box->isEmpty()) {
      const QSizeF box(*state.box);
      if (const auto saved = find(CropPresetKind::UserSize, box); saved >= 0) {
        return saved;
      }
      if (const auto built_in = find(CropPresetKind::Size, box); built_in >= 0) {
        return built_in;
      }
    }
    return find(CropPresetKind::FreeSize);
  }
  if (state.ratio.width() > 0.0 && state.ratio.height() > 0.0) {
    if (const auto saved = find(CropPresetKind::UserRatio, state.ratio); saved >= 0) {
      return saved;
    }
    if (const auto original = crop_original_ratio(state.document);
        !original.isEmpty() && same_value(original, state.ratio)) {
      if (const auto row = find(CropPresetKind::OriginalRatio); row >= 0) {
        return row;
      }
    }
    if (const auto built_in = find(CropPresetKind::Ratio, state.ratio); built_in >= 0) {
      return built_in;
    }
  }
  return find(CropPresetKind::FreeRatio);
}

QSizeF crop_original_ratio(QSize document) {
  if (document.width() <= 0 || document.height() <= 0) {
    return {};
  }
  const auto divisor = std::gcd(document.width(), document.height());
  return QSizeF(document.width() / divisor, document.height() / divisor);
}

QString crop_ratio_preset_name(QSizeF ratio) {
  return QStringLiteral("%1 : %2").arg(trimmed_number(ratio.width()), trimmed_number(ratio.height()));
}

QString crop_size_preset_name(QSize size) {
  return QObject::tr("%1 x %2 px").arg(size.width()).arg(size.height());
}

}  // namespace patchy::ui
